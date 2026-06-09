package main

import (
	"database/sql"
	"encoding/json"
	"log"
	"net/http"
	"sync"
	"time"

	"github.com/gorilla/websocket"
	"golang.org/x/crypto/bcrypt"
)

type WSRequest struct {
	ID       int             `json:"id"`
	Method   string          `json:"method"`
	Params   json.RawMessage `json:"params"`
	Seq      int             `json:"seq"`
	PrevHash string          `json:"prev_hash"`
}

type WSResponse struct {
	ID      int    `json:"id"`
	Status  int    `json:"status"`
	Message string `json:"message"`
	Data    any    `json:"data,omitempty"`
}

type ExamClient struct {
	conn      *websocket.Conn
	mu        sync.Mutex
	Role      string // "student" or "teacher"
	StudentID string // set for students
	TeacherID string // set for teachers
	RunID     string
	Name      string
	Cheating  *CheatingState // set for students after login
	Finished  bool
}

func (c *ExamClient) Send(resp WSResponse) {
	c.mu.Lock()
	defer c.mu.Unlock()
	logDebug("ws send to %s: id=%d status=%d msg=%s", c.conn.RemoteAddr(), resp.ID, resp.Status, resp.Message)
	c.conn.WriteJSON(resp)
}

var upgrader = websocket.Upgrader{
	CheckOrigin: func(r *http.Request) bool { return true },
}

var (
	clientsMu sync.RWMutex
	clients   = map[*ExamClient]struct{}{}
)

func addClient(c *ExamClient) {
	clientsMu.Lock()
	clients[c] = struct{}{}
	clientsMu.Unlock()
}

func removeClient(c *ExamClient) {
	clientsMu.Lock()
	delete(clients, c)
	clientsMu.Unlock()
}

func handleWebSocket(db *sql.DB) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		conn, err := upgrader.Upgrade(w, r, nil)
		if err != nil {
			log.Printf("ws upgrade error: %v", err)
			return
		}
		defer conn.Close()

		client := &ExamClient{conn: conn}
		addClient(client)
		defer removeClient(client)

		log.Printf("ws: client connected from %s", conn.RemoteAddr())

		for {
			_, message, err := conn.ReadMessage()
			if err != nil {
				log.Printf("ws: read error: %v", err)
				break
			}

			logDebug("ws recv from %s: %s", conn.RemoteAddr(), string(message))

			var req WSRequest
			if err := json.Unmarshal(message, &req); err != nil {
				client.Send(WSResponse{ID: 0, Status: -1, Message: "invalid json"})
				continue
			}

			hadCheating := client.Cheating != nil

			if hadCheating {
				if !client.Cheating.VerifyChain(message, req.Seq, req.PrevHash) {
					client.Cheating.SaveToDB(db)
					notifyTeachersFlag(client.RunID, client.StudentID, "chain_broken",
						"message chain integrity violated — possible proxy/tampering", "high")
				}
			}

			handleWSMethod(db, client, req)

			if !hadCheating && client.Cheating != nil {
				client.Cheating.mu.Lock()
				client.Cheating.ExpectedSeq = req.Seq + 1
				client.Cheating.LastMsgHash = computeHash(message)
				client.Cheating.mu.Unlock()
			}
		}

		if client.Cheating != nil {
			log.Printf("ws: student disconnected %s (%s), heartbeat checker will flag", client.Name, client.StudentID)
			notifyTeachersStatus(client, false)
		}
		log.Printf("ws: client disconnected: %s (%s: %s)", conn.RemoteAddr(), client.Role, client.Name)
	}
}

func handleWSMethod(db *sql.DB, client *ExamClient, req WSRequest) {
	switch req.Method {
	case "login":
		wsLogin(db, client, req)
	case "teacher_login":
		wsTeacherLogin(db, client, req)
	case "event":
		wsEvent(db, client, req)
	default:
		client.Send(WSResponse{ID: req.ID, Status: -1, Message: "unknown method"})
	}
}

func getRunSettings(db *sql.DB, runID string) (string, string) {
	var examID string
	db.QueryRow("SELECT exam_id FROM exam_runs WHERE id = ?", runID).Scan(&examID)
	if examID == "" {
		return "{}", ""
	}
	var settings string
	db.QueryRow("SELECT settings FROM exams WHERE id = ?", examID).Scan(&settings)
	if settings == "" {
		settings = "{}"
	}
	return settings, examID
}

type RunTimeInfo struct {
	Status           string     `json:"status"`
	StartedAt        *time.Time `json:"started_at,omitempty"`
	DurationMinutes  int        `json:"duration_minutes"`
	RemainingSeconds int        `json:"remaining_seconds"`
	ExamEndsAt       *time.Time `json:"exam_ends_at,omitempty"`
}

func getRunTimeInfo(db *sql.DB, runID string) RunTimeInfo {
	var status string
	var startedAt sql.NullTime
	db.QueryRow("SELECT status, started_at FROM exam_runs WHERE id = ?", runID).Scan(&status, &startedAt)

	settings, _ := getRunSettings(db, runID)
	var s struct {
		ExamDuration int `json:"exam_duration"`
	}
	json.Unmarshal([]byte(settings), &s)

	info := RunTimeInfo{
		Status:          status,
		DurationMinutes: s.ExamDuration,
	}

	if startedAt.Valid && s.ExamDuration > 0 {
		info.StartedAt = &startedAt.Time
		endsAt := startedAt.Time.Add(time.Duration(s.ExamDuration) * time.Minute)
		info.ExamEndsAt = &endsAt
		remaining := int(time.Until(endsAt).Seconds())
		if remaining < 0 {
			remaining = 0
		}
		info.RemainingSeconds = remaining
	}

	return info
}

// login params: {"name":"...", "run_id":"..."}
func wsLogin(db *sql.DB, client *ExamClient, req WSRequest) {
	var params struct {
		Name  string `json:"name"`
		RunID string `json:"run_id"`
	}
	if err := json.Unmarshal(req.Params, &params); err != nil || params.Name == "" || params.RunID == "" {
		client.Send(WSResponse{ID: req.ID, Status: -1, Message: "params must have name and run_id"})
		return
	}

	name := params.Name
	runID := params.RunID

	var student RegisteredStudent
	err := db.QueryRow(
		"SELECT id, run_id, name, stats FROM registered_students WHERE name = ? AND run_id = ?",
		name, runID,
	).Scan(&student.ID, &student.RunID, &student.Name, &student.Stats)

	if err == sql.ErrNoRows {
		client.Send(WSResponse{ID: req.ID, Status: 1, Message: "student not found"})
		return
	}
	if err != nil {
		log.Printf("ws login query: %v", err)
		client.Send(WSResponse{ID: req.ID, Status: -1, Message: "internal error"})
		return
	}

	var runStatus string
	db.QueryRow("SELECT status FROM exam_runs WHERE id = ?", runID).Scan(&runStatus)
	if runStatus == "finished" {
		client.Send(WSResponse{ID: req.ID, Status: 3, Message: "exam has ended"})
		return
	}
	if runStatus == "pending" {
		client.Send(WSResponse{ID: req.ID, Status: 4, Message: "exam has not started yet"})
		return
	}

	var finishedCount int
	db.QueryRow("SELECT COUNT(*) FROM events WHERE student_id = ? AND type = 'student_finished'", student.ID).Scan(&finishedCount)
	if finishedCount > 0 {
		client.Send(WSResponse{ID: req.ID, Status: 5, Message: "you have already finished this exam"})
		return
	}

	clientsMu.RLock()
	for c := range clients {
		if c != client && c.StudentID == student.ID {
			clientsMu.RUnlock()
			log.Printf("ws: DUPLICATE LOGIN BLOCKED — student %s (%s) already connected from %s, new attempt from %s",
				student.Name, student.ID, c.conn.RemoteAddr(), client.conn.RemoteAddr())
			client.Send(WSResponse{ID: req.ID, Status: 2, Message: "student already connected from another session"})
			return
		}
	}
	clientsMu.RUnlock()

	client.Role = "student"
	client.StudentID = student.ID
	client.RunID = student.RunID
	client.Name = student.Name
	client.Cheating = GetCheatingState(student.ID, student.RunID)
	client.Cheating.ResetChain()
	client.Cheating.mu.Lock()
	client.Cheating.StudentName = student.Name
	client.Cheating.Connected = true
	client.Cheating.LastHeartbeat = time.Now()
	client.Cheating.mu.Unlock()

	log.Printf("ws: student logged in: %s (%s) for run %s", student.Name, student.ID, student.RunID)

	client.Cheating.SaveToDB(db)
	notifyTeachersStatus(client, true)

	settings, examID := getRunSettings(db, student.RunID)
	timeInfo := getRunTimeInfo(db, student.RunID)

	var parsedSettings struct {
		PredefinedFiles []string `json:"predefined_files"`
	}
	json.Unmarshal([]byte(settings), &parsedSettings)

	fileURLs := []string{}
	for _, f := range parsedSettings.PredefinedFiles {
		fileURLs = append(fileURLs, "/exam/files/"+examID+"/"+f)
	}

	client.Send(WSResponse{
		ID:      req.ID,
		Status:  0,
		Message: "success",
		Data: map[string]any{
			"student_id":        student.ID,
			"name":              student.Name,
			"run_id":            student.RunID,
			"exam_id":           examID,
			"settings":          json.RawMessage(settings),
			"remaining_seconds": timeInfo.RemainingSeconds,
			"exam_ends_at":      timeInfo.ExamEndsAt,
			"status":            timeInfo.Status,
			"exam_files":        fileURLs,
		},
	})
}

// event params: {"type":"clipboard_text","detail":{...}}
func wsEvent(db *sql.DB, client *ExamClient, req WSRequest) {
	if client.StudentID == "" {
		client.Send(WSResponse{ID: req.ID, Status: -1, Message: "not logged in"})
		return
	}

	var params struct {
		Type   string          `json:"type"`
		Detail json.RawMessage `json:"detail"`
	}
	if err := json.Unmarshal(req.Params, &params); err != nil || params.Type == "" {
		client.Send(WSResponse{ID: req.ID, Status: -1, Message: "params must have type"})
		return
	}

	if client.Finished && params.Type != "student_finished" {
		client.Send(WSResponse{ID: req.ID, Status: -1, Message: "exam already finished"})
		return
	}

	var runStatus string
	db.QueryRow("SELECT status FROM exam_runs WHERE id = ?", client.RunID).Scan(&runStatus)
	if runStatus != "running" {
		client.Send(WSResponse{ID: req.ID, Status: -1, Message: "exam is not active"})
		return
	}

	detailStr := "{}"
	if len(params.Detail) > 0 {
		detailStr = string(params.Detail)
	}

	id := genID()
	_, err := db.Exec(
		"INSERT INTO events (id, student_id, run_id, type, detail, created_at) VALUES (?, ?, ?, ?, ?, ?)",
		id, client.StudentID, client.RunID, params.Type, detailStr, time.Now().UTC(),
	)
	if err != nil {
		log.Printf("ws event insert: %v", err)
		client.Send(WSResponse{ID: req.ID, Status: -1, Message: "internal error"})
		return
	}

	if params.Type == "student_finished" {
		client.Finished = true
		if client.Cheating != nil {
			client.Cheating.mu.Lock()
			client.Cheating.Finished = true
			client.Cheating.mu.Unlock()
		}
	}

	log.Printf("ws: event from %s: %s", client.Name, params.Type)

	if client.Cheating != nil {
		flagsBefore := len(client.Cheating.Flags)
		client.Cheating.Update(params.Type, params.Detail)
		if len(client.Cheating.Flags) > flagsBefore {
			client.Cheating.SaveToDB(db)
			for _, f := range client.Cheating.Flags[flagsBefore:] {
				notifyTeachersFlag(client.RunID, client.StudentID, f.Type, f.Detail, f.Severity)
			}
		}
	}

	logDebug("cheating_state for %s: %s", client.Name, client.Cheating.ToString())

	notifyTeachers(client, params.Type, detailStr)

	client.Send(WSResponse{ID: req.ID, Status: 0, Message: "ok"})
}

// teacher_login params: {"name":"...", "password":"...", "run_id":"..."}
func wsTeacherLogin(db *sql.DB, client *ExamClient, req WSRequest) {
	var params struct {
		Name     string `json:"name"`
		Password string `json:"password"`
		RunID    string `json:"run_id"`
	}
	if err := json.Unmarshal(req.Params, &params); err != nil || params.Name == "" || params.Password == "" || params.RunID == "" {
		client.Send(WSResponse{ID: req.ID, Status: -1, Message: "params must have name, password, and run_id"})
		return
	}

	var teacher Teacher
	err := db.QueryRow(
		"SELECT id, name, password_hash FROM teachers WHERE name = ?",
		params.Name,
	).Scan(&teacher.ID, &teacher.Name, &teacher.PasswordHash)

	if err == sql.ErrNoRows {
		client.Send(WSResponse{ID: req.ID, Status: 1, Message: "teacher not found"})
		return
	}
	if err != nil {
		log.Printf("ws teacher_login query: %v", err)
		client.Send(WSResponse{ID: req.ID, Status: -1, Message: "internal error"})
		return
	}

	if err := bcrypt.CompareHashAndPassword([]byte(teacher.PasswordHash), []byte(params.Password)); err != nil {
		client.Send(WSResponse{ID: req.ID, Status: 1, Message: "invalid password"})
		return
	}

	client.Role = "teacher"
	client.TeacherID = teacher.ID
	client.RunID = params.RunID
	client.Name = teacher.Name

	log.Printf("ws: teacher logged in: %s (%s) watching run %s", teacher.Name, teacher.ID, params.RunID)

	settings, _ := getRunSettings(db, params.RunID)
	timeInfo := getRunTimeInfo(db, params.RunID)

	client.Send(WSResponse{
		ID:      req.ID,
		Status:  0,
		Message: "success",
		Data: map[string]any{
			"teacher_id":        teacher.ID,
			"name":              teacher.Name,
			"run_id":            params.RunID,
			"settings":          json.RawMessage(settings),
			"remaining_seconds": timeInfo.RemainingSeconds,
			"exam_ends_at":      timeInfo.ExamEndsAt,
			"status":            timeInfo.Status,
		},
	})
}

func notifyTeachers(student *ExamClient, eventType string, detail string) {
	clientsMu.RLock()
	defer clientsMu.RUnlock()

	for c := range clients {
		if c.Role == "teacher" && c.RunID == student.RunID {
			c.Send(WSResponse{
				ID:      0,
				Status:  0,
				Message: "student_event",
				Data: map[string]string{
					"student_name": student.Name,
					"student_id":   student.StudentID,
					"type":         eventType,
					"detail":       detail,
				},
			})
		}
	}
}

func notifyTeachersStatus(student *ExamClient, online bool) {
	clientsMu.RLock()
	defer clientsMu.RUnlock()

	status := "offline"
	if online {
		status = "online"
	}

	for c := range clients {
		if c.Role == "teacher" && c.RunID == student.RunID {
			c.Send(WSResponse{
				ID:      0,
				Status:  0,
				Message: "student_status",
				Data: map[string]string{
					"student_name": student.Name,
					"student_id":   student.StudentID,
					"status":       status,
				},
			})
		}
	}
}
