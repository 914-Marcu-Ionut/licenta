package main

import (
	"archive/zip"
	"crypto/rand"
	"database/sql"
	"encoding/hex"
	"encoding/json"
	"io"
	"log"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"time"

	"golang.org/x/crypto/bcrypt"
)

func genID() string {
	b := make([]byte, 4)
	rand.Read(b)
	return hex.EncodeToString(b)
}

func writeJSON(w http.ResponseWriter, status int, v any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	if debugMode {
		data, _ := json.Marshal(v)
		logDebug("http resp %d: %s", status, string(data))
	}
	json.NewEncoder(w).Encode(v)
}

func writeError(w http.ResponseWriter, status int, msg string) {
	logDebug("http error %d: %s", status, msg)
	writeJSON(w, status, map[string]string{"error": msg})
}

// POST /register  { "name": "...", "password": "..." }
func handleRegister(db *sql.DB) http.HandlerFunc {
	type request struct {
		Name     string `json:"name"`
		Password string `json:"password"`
	}
	return func(w http.ResponseWriter, r *http.Request) {
		var req request
		if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
			writeError(w, http.StatusBadRequest, "invalid json")
			return
		}
		if req.Name == "" || req.Password == "" {
			writeError(w, http.StatusBadRequest, "name and password required")
			return
		}

		hash, err := bcrypt.GenerateFromPassword([]byte(req.Password), bcrypt.DefaultCost)
		if err != nil {
			log.Printf("bcrypt error: %v", err)
			writeError(w, http.StatusInternalServerError, "internal error")
			return
		}

		id := genID()
		_, err = db.Exec(
			"INSERT INTO teachers (id, name, password_hash) VALUES (?, ?, ?)",
			id, req.Name, string(hash),
		)
		if err != nil {
			log.Printf("insert teacher: %v", err)
			writeError(w, http.StatusInternalServerError, "could not create teacher")
			return
		}

		writeJSON(w, http.StatusCreated, Teacher{ID: id, Name: req.Name})
	}
}

// POST /login  { "name": "...", "password": "..." }
func handleLogin(db *sql.DB) http.HandlerFunc {
	type request struct {
		Name     string `json:"name"`
		Password string `json:"password"`
	}
	return func(w http.ResponseWriter, r *http.Request) {
		var req request
		if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
			writeError(w, http.StatusBadRequest, "invalid json")
			return
		}
		if req.Name == "" || req.Password == "" {
			writeError(w, http.StatusBadRequest, "name and password required")
			return
		}

		var t Teacher
		err := db.QueryRow(
			"SELECT id, name, password_hash FROM teachers WHERE name = ?", req.Name,
		).Scan(&t.ID, &t.Name, &t.PasswordHash)
		if err == sql.ErrNoRows {
			writeError(w, http.StatusUnauthorized, "invalid credentials")
			return
		}
		if err != nil {
			log.Printf("query teacher: %v", err)
			writeError(w, http.StatusInternalServerError, "internal error")
			return
		}

		if err := bcrypt.CompareHashAndPassword([]byte(t.PasswordHash), []byte(req.Password)); err != nil {
			writeError(w, http.StatusUnauthorized, "invalid credentials")
			return
		}

		writeJSON(w, http.StatusOK, Teacher{ID: t.ID, Name: t.Name})
	}
}

// POST /exam  { "created_by": "teacher-id", "name": "...", "settings": "{...}" }
func handleCreateExam(db *sql.DB) http.HandlerFunc {
	type request struct {
		CreatedBy string `json:"created_by"`
		Name      string `json:"name"`
		Settings  string `json:"settings"`
	}
	return func(w http.ResponseWriter, r *http.Request) {
		var req request
		if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
			writeError(w, http.StatusBadRequest, "invalid json")
			return
		}
		if req.CreatedBy == "" || req.Name == "" {
			writeError(w, http.StatusBadRequest, "created_by and name required")
			return
		}
		if req.Settings == "" {
			req.Settings = "{}"
		}

		var exists bool
		db.QueryRow("SELECT 1 FROM teachers WHERE id = ?", req.CreatedBy).Scan(&exists)
		if !exists {
			writeError(w, http.StatusBadRequest, "teacher not found")
			return
		}

		id := genID()
		now := time.Now().UTC()
		_, err := db.Exec(
			"INSERT INTO exams (id, created_by, name, settings, created_at) VALUES (?, ?, ?, ?, ?)",
			id, req.CreatedBy, req.Name, req.Settings, now,
		)
		if err != nil {
			log.Printf("insert exam: %v", err)
			writeError(w, http.StatusInternalServerError, "could not create exam")
			return
		}

		writeJSON(w, http.StatusCreated, Exam{
			ID:        id,
			CreatedBy: req.CreatedBy,
			Name:      req.Name,
			Settings:  req.Settings,
			CreatedAt: now,
		})
	}
}

// PUT /exam  { "id": "...", "name": "...", "settings": "..." }
func handleUpdateExam(db *sql.DB) http.HandlerFunc {
	type request struct {
		ID       string `json:"id"`
		Name     string `json:"name"`
		Settings string `json:"settings"`
	}
	return func(w http.ResponseWriter, r *http.Request) {
		var req request
		if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
			writeError(w, http.StatusBadRequest, "invalid json")
			return
		}
		if req.ID == "" {
			writeError(w, http.StatusBadRequest, "id required")
			return
		}

		var exists bool
		db.QueryRow("SELECT 1 FROM exams WHERE id = ?", req.ID).Scan(&exists)
		if !exists {
			writeError(w, http.StatusNotFound, "exam not found")
			return
		}

		var hasRunning bool
		db.QueryRow("SELECT 1 FROM exam_runs WHERE exam_id = ? AND status = 'running'", req.ID).Scan(&hasRunning)
		if hasRunning {
			writeError(w, http.StatusConflict, "cannot edit exam while a run is active")
			return
		}

		if req.Name != "" {
			db.Exec("UPDATE exams SET name = ? WHERE id = ?", req.Name, req.ID)
		}
		if req.Settings != "" {
			db.Exec("UPDATE exams SET settings = ? WHERE id = ?", req.Settings, req.ID)
		}

		var exam Exam
		db.QueryRow("SELECT id, created_by, name, settings, created_at FROM exams WHERE id = ?", req.ID).Scan(
			&exam.ID, &exam.CreatedBy, &exam.Name, &exam.Settings, &exam.CreatedAt,
		)

		writeJSON(w, http.StatusOK, exam)
	}
}

// DELETE /exam?id=...
func handleDeleteExam(db *sql.DB) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		examID := r.URL.Query().Get("id")
		if examID == "" {
			writeError(w, http.StatusBadRequest, "id parameter required")
			return
		}

		var exists bool
		db.QueryRow("SELECT 1 FROM exams WHERE id = ?", examID).Scan(&exists)
		if !exists {
			writeError(w, http.StatusNotFound, "exam not found")
			return
		}

		// Delete all related data
		db.Exec("DELETE FROM events WHERE run_id IN (SELECT id FROM exam_runs WHERE exam_id = ?)", examID)
		db.Exec("DELETE FROM registered_students WHERE run_id IN (SELECT id FROM exam_runs WHERE exam_id = ?)", examID)
		db.Exec("DELETE FROM exam_runs WHERE exam_id = ?", examID)
		db.Exec("DELETE FROM exams WHERE id = ?", examID)

		// Remove files from disk
		examDir := filepath.Join(examFilesDir, examID)
		os.RemoveAll(examDir)

		log.Printf("exam %s: deleted with all runs and files", examID)
		writeJSON(w, http.StatusOK, map[string]string{"status": "deleted"})
	}
}

// DELETE /exam/run?id=...
func handleDeleteExamRun(db *sql.DB) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		runID := r.URL.Query().Get("id")
		if runID == "" {
			writeError(w, http.StatusBadRequest, "id parameter required")
			return
		}

		var exists bool
		db.QueryRow("SELECT 1 FROM exam_runs WHERE id = ?", runID).Scan(&exists)
		if !exists {
			writeError(w, http.StatusNotFound, "run not found")
			return
		}

		db.Exec("DELETE FROM events WHERE run_id = ?", runID)
		db.Exec("DELETE FROM registered_students WHERE run_id = ?", runID)
		db.Exec("DELETE FROM exam_runs WHERE id = ?", runID)

		log.Printf("exam run %s: deleted with all students and events", runID)
		writeJSON(w, http.StatusOK, map[string]string{"status": "deleted"})
	}
}

// GET /exams
func handleListExams(db *sql.DB) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		examRows, err := db.Query("SELECT id, created_by, name, settings, created_at FROM exams ORDER BY created_at DESC")
		if err != nil {
			log.Printf("query exams: %v", err)
			writeError(w, http.StatusInternalServerError, "internal error")
			return
		}
		defer examRows.Close()

		exams := []Exam{}
		for examRows.Next() {
			var e Exam
			if err := examRows.Scan(&e.ID, &e.CreatedBy, &e.Name, &e.Settings, &e.CreatedAt); err != nil {
				log.Printf("scan exam: %v", err)
				continue
			}
			e.Runs = []ExamRun{}
			exams = append(exams, e)
		}

		for i, e := range exams {
			runRows, err := db.Query(
				"SELECT id, exam_id, status, started_at, ended_at FROM exam_runs WHERE exam_id = ?", e.ID,
			)
			if err != nil {
				log.Printf("query runs: %v", err)
				continue
			}
			for runRows.Next() {
				var run ExamRun
				var startedAt, endedAt sql.NullTime
				if err := runRows.Scan(&run.ID, &run.ExamID, &run.Status, &startedAt, &endedAt); err != nil {
					log.Printf("scan run: %v", err)
					continue
				}
				if startedAt.Valid {
					run.StartedAt = startedAt.Time
				}
				if endedAt.Valid {
					run.EndedAt = endedAt.Time
				}
				run.RegisteredStudents = []RegisteredStudent{}

				studentRows, err := db.Query(
					"SELECT id, run_id, name, stats FROM registered_students WHERE run_id = ?", run.ID,
				)
				if err == nil {
					for studentRows.Next() {
						var s RegisteredStudent
						if err := studentRows.Scan(&s.ID, &s.RunID, &s.Name, &s.Stats); err == nil {
							run.RegisteredStudents = append(run.RegisteredStudents, s)
						}
					}
					studentRows.Close()
				}
				exams[i].Runs = append(exams[i].Runs, run)
			}
			runRows.Close()
		}

		writeJSON(w, http.StatusOK, exams)
	}
}

// GET /exam?id=...
func handleGetExam(db *sql.DB) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		examID := r.URL.Query().Get("id")
		if examID == "" {
			writeError(w, http.StatusBadRequest, "id parameter required")
			return
		}

		var exam Exam
		err := db.QueryRow(
			"SELECT id, created_by, name, settings, created_at FROM exams WHERE id = ?", examID,
		).Scan(&exam.ID, &exam.CreatedBy, &exam.Name, &exam.Settings, &exam.CreatedAt)
		if err == sql.ErrNoRows {
			writeError(w, http.StatusNotFound, "exam not found")
			return
		}
		if err != nil {
			log.Printf("query exam: %v", err)
			writeError(w, http.StatusInternalServerError, "internal error")
			return
		}

		exam.Runs = []ExamRun{}

		rows, err := db.Query(
			"SELECT id, exam_id, status, started_at, ended_at FROM exam_runs WHERE exam_id = ?", examID,
		)
		if err != nil {
			log.Printf("query runs: %v", err)
			writeError(w, http.StatusInternalServerError, "internal error")
			return
		}
		defer rows.Close()

		for rows.Next() {
			var run ExamRun
			var startedAt, endedAt sql.NullTime
			if err := rows.Scan(&run.ID, &run.ExamID, &run.Status, &startedAt, &endedAt); err != nil {
				log.Printf("scan run: %v", err)
				continue
			}
			if startedAt.Valid {
				run.StartedAt = startedAt.Time
			}
			if endedAt.Valid {
				run.EndedAt = endedAt.Time
			}

			studentRows, err := db.Query(
				"SELECT id, run_id, name, stats FROM registered_students WHERE run_id = ?", run.ID,
			)
			if err != nil {
				log.Printf("query students: %v", err)
			} else {
				for studentRows.Next() {
					var s RegisteredStudent
					if err := studentRows.Scan(&s.ID, &s.RunID, &s.Name, &s.Stats); err != nil {
						log.Printf("scan student: %v", err)
						continue
					}
					run.RegisteredStudents = append(run.RegisteredStudents, s)
				}
				studentRows.Close()
			}

			exam.Runs = append(exam.Runs, run)
		}

		writeJSON(w, http.StatusOK, exam)
	}
}

// GET /exam/run?id=...  (returns run info + parent exam name)
func handleGetExamRun(db *sql.DB) http.HandlerFunc {
	type response struct {
		ExamRun
		ExamName     string `json:"exam_name"`
		ExamSettings string `json:"exam_settings"`
	}
	return func(w http.ResponseWriter, r *http.Request) {
		runID := r.URL.Query().Get("id")
		if runID == "" {
			writeError(w, http.StatusBadRequest, "id parameter required")
			return
		}

		var run ExamRun
		var startedAt, endedAt sql.NullTime
		err := db.QueryRow(
			"SELECT id, exam_id, status, started_at, ended_at FROM exam_runs WHERE id = ?", runID,
		).Scan(&run.ID, &run.ExamID, &run.Status, &startedAt, &endedAt)
		if err == sql.ErrNoRows {
			writeError(w, http.StatusNotFound, "run not found")
			return
		}
		if err != nil {
			log.Printf("query run: %v", err)
			writeError(w, http.StatusInternalServerError, "internal error")
			return
		}
		if startedAt.Valid {
			run.StartedAt = startedAt.Time
		}
		if endedAt.Valid {
			run.EndedAt = endedAt.Time
		}

		var examName, examSettings string
		db.QueryRow("SELECT name, settings FROM exams WHERE id = ?", run.ExamID).Scan(&examName, &examSettings)

		run.RegisteredStudents = []RegisteredStudent{}
		studentRows, err := db.Query(
			"SELECT id, run_id, name, stats FROM registered_students WHERE run_id = ?", run.ID,
		)
		if err == nil {
			for studentRows.Next() {
				var s RegisteredStudent
				if err := studentRows.Scan(&s.ID, &s.RunID, &s.Name, &s.Stats); err == nil {
					run.RegisteredStudents = append(run.RegisteredStudents, s)
				}
			}
			studentRows.Close()
		}

		writeJSON(w, http.StatusOK, response{ExamRun: run, ExamName: examName, ExamSettings: examSettings})
	}
}

// POST /exam/run/register  { "run_id": "...", "name": "..." }
func handleRegisterStudent(db *sql.DB) http.HandlerFunc {
	type request struct {
		RunID string `json:"run_id"`
		Name  string `json:"name"`
	}
	return func(w http.ResponseWriter, r *http.Request) {
		var req request
		if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
			writeError(w, http.StatusBadRequest, "invalid json")
			return
		}
		if req.RunID == "" || req.Name == "" {
			writeError(w, http.StatusBadRequest, "run_id and name required")
			return
		}

		var exists bool
		db.QueryRow("SELECT 1 FROM exam_runs WHERE id = ?", req.RunID).Scan(&exists)
		if !exists {
			writeError(w, http.StatusNotFound, "run not found")
			return
		}

		var existing RegisteredStudent
		err := db.QueryRow(
			"SELECT id, run_id, name, stats FROM registered_students WHERE run_id = ? AND name = ?",
			req.RunID, req.Name,
		).Scan(&existing.ID, &existing.RunID, &existing.Name, &existing.Stats)
		if err == nil {
			writeJSON(w, http.StatusOK, existing)
			return
		}

		id := genID()
		_, insertErr := db.Exec(
			"INSERT INTO registered_students (id, run_id, name, stats) VALUES (?, ?, ?, '{}')",
			id, req.RunID, req.Name,
		)
		if insertErr != nil {
			log.Printf("insert student: %v", insertErr)
			writeError(w, http.StatusInternalServerError, "could not register student")
			return
		}

		writeJSON(w, http.StatusCreated, RegisteredStudent{
			ID:    id,
			RunID: req.RunID,
			Name:  req.Name,
			Stats: "{}",
		})
	}
}

// POST /exam/run  { "exam_id": "..." }
func handleCreateExamRun(db *sql.DB) http.HandlerFunc {
	type request struct {
		ExamID string `json:"exam_id"`
	}
	return func(w http.ResponseWriter, r *http.Request) {
		var req request
		if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
			writeError(w, http.StatusBadRequest, "invalid json")
			return
		}
		if req.ExamID == "" {
			writeError(w, http.StatusBadRequest, "exam_id required")
			return
		}

		var exists bool
		db.QueryRow("SELECT 1 FROM exams WHERE id = ?", req.ExamID).Scan(&exists)
		if !exists {
			writeError(w, http.StatusBadRequest, "exam not found")
			return
		}

		id := genID()
		_, err := db.Exec(
			"INSERT INTO exam_runs (id, exam_id, status) VALUES (?, ?, 'pending')",
			id, req.ExamID,
		)
		if err != nil {
			log.Printf("insert exam run: %v", err)
			writeError(w, http.StatusInternalServerError, "could not create exam run")
			return
		}

		writeJSON(w, http.StatusCreated, ExamRun{
			ID:     id,
			ExamID: req.ExamID,
			Status: "pending",
		})
	}
}

// PUT /exam/run/status  { "run_id": "...", "status": "running"|"finished" }
func handleUpdateRunStatus(db *sql.DB) http.HandlerFunc {
	type request struct {
		RunID  string `json:"run_id"`
		Status string `json:"status"`
	}
	return func(w http.ResponseWriter, r *http.Request) {
		var req request
		if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
			writeError(w, http.StatusBadRequest, "invalid json")
			return
		}
		if req.RunID == "" || req.Status == "" {
			writeError(w, http.StatusBadRequest, "run_id and status required")
			return
		}
		if req.Status != "pending" && req.Status != "running" && req.Status != "finished" {
			writeError(w, http.StatusBadRequest, "status must be pending, running, or finished")
			return
		}

		var exists bool
		db.QueryRow("SELECT 1 FROM exam_runs WHERE id = ?", req.RunID).Scan(&exists)
		if !exists {
			writeError(w, http.StatusNotFound, "run not found")
			return
		}

		now := time.Now().UTC()
		switch req.Status {
		case "running":
			db.Exec("UPDATE exam_runs SET status = ?, started_at = ? WHERE id = ?", req.Status, now, req.RunID)
		case "finished":
			db.Exec("UPDATE exam_runs SET status = ?, ended_at = ? WHERE id = ?", req.Status, now, req.RunID)
			notifyRunEnded(req.RunID)
		default:
			db.Exec("UPDATE exam_runs SET status = ? WHERE id = ?", req.Status, req.RunID)
		}

		writeJSON(w, http.StatusOK, map[string]string{"status": req.Status})
	}
}

// GET /student/events?id=...
func handleGetStudentEvents(db *sql.DB) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		studentID := r.URL.Query().Get("id")
		if studentID == "" {
			writeError(w, http.StatusBadRequest, "id parameter required")
			return
		}

		rows, err := db.Query(
			"SELECT id, student_id, run_id, type, detail, created_at FROM events WHERE student_id = ? AND type NOT IN ('initial_snapshot', 'heartbeat') ORDER BY created_at DESC LIMIT 200",
			studentID,
		)
		if err != nil {
			log.Printf("query student events: %v", err)
			writeError(w, http.StatusInternalServerError, "internal error")
			return
		}
		defer rows.Close()

		events := []Event{}
		for rows.Next() {
			var e Event
			if err := rows.Scan(&e.ID, &e.StudentID, &e.RunID, &e.Type, &e.Detail, &e.CreatedAt); err != nil {
				log.Printf("scan event: %v", err)
				continue
			}
			events = append(events, e)
		}

		writeJSON(w, http.StatusOK, events)
	}
}

// POST /student/clear-flags  { "student_id": "..." }
func handleClearFlags(db *sql.DB) http.HandlerFunc {
	type request struct {
		StudentID string `json:"student_id"`
	}
	return func(w http.ResponseWriter, r *http.Request) {
		var req request
		if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
			writeError(w, http.StatusBadRequest, "invalid json")
			return
		}
		if req.StudentID == "" {
			writeError(w, http.StatusBadRequest, "student_id required")
			return
		}

		cheatingMu.RLock()
		cs, exists := cheatingStates[req.StudentID]
		cheatingMu.RUnlock()

		if exists {
			cs.mu.Lock()
			cs.Flags = []Flag{}
			cs.Score = 0
			cs.mu.Unlock()
			cs.SaveToDB(db)
		} else {
			db.Exec("UPDATE registered_students SET stats = '{}' WHERE id = ?", req.StudentID)
		}

		writeJSON(w, http.StatusOK, map[string]string{"status": "cleared"})
	}
}

const examFilesDir = "../exam_files"

// POST /exam/files  (multipart form: exam_id + files)
func handleUploadExamFiles() http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		r.Body = http.MaxBytesReader(w, r.Body, 500<<20) // 500MB max

		if err := r.ParseMultipartForm(100 << 20); err != nil {
			writeError(w, http.StatusBadRequest, "failed to parse form: "+err.Error())
			return
		}

		examID := r.FormValue("exam_id")
		if examID == "" {
			writeError(w, http.StatusBadRequest, "exam_id required")
			return
		}

		destDir := filepath.Join(examFilesDir, examID)
		if err := os.MkdirAll(destDir, 0755); err != nil {
			log.Printf("mkdir exam files: %v", err)
			writeError(w, http.StatusInternalServerError, "failed to create directory")
			return
		}

		files := r.MultipartForm.File["files"]
		saved := []string{}

		for _, fh := range files {
			name := filepath.Clean(fh.Filename)
			name = strings.ReplaceAll(name, "\\", "/")

			filePath := filepath.Join(destDir, name)
			if err := os.MkdirAll(filepath.Dir(filePath), 0755); err != nil {
				log.Printf("mkdir for file %s: %v", name, err)
				continue
			}

			src, err := fh.Open()
			if err != nil {
				log.Printf("open upload %s: %v", name, err)
				continue
			}

			dst, err := os.Create(filePath)
			if err != nil {
				src.Close()
				log.Printf("create file %s: %v", filePath, err)
				continue
			}

			_, err = io.Copy(dst, src)
			src.Close()
			dst.Close()
			if err != nil {
				log.Printf("write file %s: %v", filePath, err)
				continue
			}

			saved = append(saved, name)
		}

		log.Printf("exam %s: uploaded %d files", examID, len(saved))
		writeJSON(w, http.StatusOK, map[string]any{
			"exam_id": examID,
			"files":   saved,
			"count":   len(saved),
		})
	}
}

// POST /exam/files/delete  { "exam_id": "...", "files": ["path1", "path2"] }
func handleDeleteExamFiles() http.HandlerFunc {
	type request struct {
		ExamID string   `json:"exam_id"`
		Files  []string `json:"files"`
	}
	return func(w http.ResponseWriter, r *http.Request) {
		var req request
		if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
			writeError(w, http.StatusBadRequest, "invalid json")
			return
		}
		if req.ExamID == "" || len(req.Files) == 0 {
			writeError(w, http.StatusBadRequest, "exam_id and files required")
			return
		}

		deleted := 0
		for _, name := range req.Files {
			name = filepath.Clean(name)
			if strings.Contains(name, "..") {
				continue
			}
			filePath := filepath.Join(examFilesDir, req.ExamID, name)
			if err := os.Remove(filePath); err == nil {
				deleted++
			}
		}

		log.Printf("exam %s: deleted %d/%d files", req.ExamID, deleted, len(req.Files))
		writeJSON(w, http.StatusOK, map[string]int{"deleted": deleted})
	}
}

// GET /exam/files/{examId}/{path...}
func handleServeExamFiles(db *sql.DB) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		path := strings.TrimPrefix(r.URL.Path, "/exam/files/")
		if path == "" || strings.Contains(path, "..") {
			writeError(w, http.StatusBadRequest, "invalid path")
			return
		}

		parts := strings.SplitN(path, "/", 2)
		examID := parts[0]

		var hasRunning bool
		db.QueryRow(
			"SELECT 1 FROM exam_runs WHERE exam_id = ? AND status = 'running'", examID,
		).Scan(&hasRunning)
		if !hasRunning {
			writeError(w, http.StatusForbidden, "exam files not available — exam is not running")
			return
		}

		filePath := filepath.Join(examFilesDir, path)
		http.ServeFile(w, r, filePath)
	}
}

const studentWorkDir = "../student_work"

// POST /student/upload-work  (multipart: student_id, run_id, file)
func handleUploadWork(db *sql.DB) http.HandlerFunc {
	const uploadGracePeriod = 2 * time.Minute

	return func(w http.ResponseWriter, r *http.Request) {
		if err := r.ParseMultipartForm(256 << 20); err != nil {
			writeError(w, http.StatusBadRequest, "invalid multipart form")
			return
		}

		studentID := r.FormValue("student_id")
		runID := r.FormValue("run_id")
		if studentID == "" || runID == "" {
			writeError(w, http.StatusBadRequest, "student_id and run_id required")
			return
		}

		var studentName string
		err := db.QueryRow("SELECT name FROM registered_students WHERE id = ? AND run_id = ?", studentID, runID).Scan(&studentName)
		if err != nil {
			writeError(w, http.StatusForbidden, "student not found in this run")
			return
		}

		// Check timing: allow upload only if exam is running OR finished within grace period
		var runStatus string
		var endedAt sql.NullString
		db.QueryRow("SELECT status, ended_at FROM exam_runs WHERE id = ?", runID).Scan(&runStatus, &endedAt)

		if runStatus == "finished" {
			if endedAt.Valid {
				ended, _ := time.Parse(time.RFC3339Nano, endedAt.String)
				if ended.IsZero() {
					ended, _ = time.Parse("2006-01-02 15:04:05", endedAt.String)
				}
				if time.Since(ended) > uploadGracePeriod {
					writeError(w, http.StatusForbidden, "upload window expired (2 minutes after exam end)")
					return
				}
			}
		} else if runStatus != "running" {
			writeError(w, http.StatusForbidden, "exam is not active")
			return
		}

		// Check if student already uploaded (one upload per student per run)
		destDir := filepath.Join(studentWorkDir, runID, studentID)
		if entries, err := os.ReadDir(destDir); err == nil && len(entries) > 0 {
			writeError(w, http.StatusConflict, "work already uploaded for this exam")
			return
		}

		file, header, err := r.FormFile("file")
		if err != nil {
			writeError(w, http.StatusBadRequest, "file field required")
			return
		}
		defer file.Close()

		os.MkdirAll(destDir, 0755)

		safeName := filepath.Base(header.Filename)
		if safeName == "" || safeName == "." {
			safeName = "work.zip"
		}
		destPath := filepath.Join(destDir, safeName)

		dst, err := os.Create(destPath)
		if err != nil {
			log.Printf("upload-work create file: %v", err)
			writeError(w, http.StatusInternalServerError, "failed to save file")
			return
		}
		defer dst.Close()

		written, err := io.Copy(dst, file)
		if err != nil {
			log.Printf("upload-work write: %v", err)
			writeError(w, http.StatusInternalServerError, "failed to write file")
			return
		}

		log.Printf("student %s (%s) uploaded work: %s (%d bytes)", studentName, studentID, safeName, written)
		writeJSON(w, http.StatusOK, map[string]any{
			"message": "work uploaded",
			"file":    safeName,
			"bytes":   written,
		})
	}
}

// GET /student/work?run_id=...
func handleListStudentWork(db *sql.DB) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		runID := r.URL.Query().Get("run_id")
		if runID == "" {
			writeError(w, http.StatusBadRequest, "run_id required")
			return
		}

		rows, err := db.Query("SELECT id, name FROM registered_students WHERE run_id = ?", runID)
		if err != nil {
			writeError(w, http.StatusInternalServerError, "query failed")
			return
		}
		defer rows.Close()

		type StudentWork struct {
			ID       string `json:"id"`
			Name     string `json:"name"`
			Uploaded bool   `json:"uploaded"`
			FileSize int64  `json:"file_size"`
		}

		var results []StudentWork
		for rows.Next() {
			var s StudentWork
			rows.Scan(&s.ID, &s.Name)

			dir := filepath.Join(studentWorkDir, runID, s.ID)
			entries, _ := os.ReadDir(dir)
			if len(entries) > 0 {
				s.Uploaded = true
				info, _ := entries[0].Info()
				if info != nil {
					s.FileSize = info.Size()
				}
			}
			results = append(results, s)
		}

		writeJSON(w, http.StatusOK, results)
	}
}

// GET /student/work/files?run_id=...&student_id=...
func handleListWorkFiles(db *sql.DB) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		runID := r.URL.Query().Get("run_id")
		studentID := r.URL.Query().Get("student_id")
		if runID == "" || studentID == "" {
			writeError(w, http.StatusBadRequest, "run_id and student_id required")
			return
		}

		dir := filepath.Join(studentWorkDir, runID, studentID)
		entries, _ := os.ReadDir(dir)
		if len(entries) == 0 {
			writeError(w, http.StatusNotFound, "no work uploaded")
			return
		}

		zipPath := filepath.Join(dir, entries[0].Name())
		zr, err := zip.OpenReader(zipPath)
		if err != nil {
			writeError(w, http.StatusInternalServerError, "failed to read zip")
			return
		}
		defer zr.Close()

		type FileEntry struct {
			Path string `json:"path"`
			Size int64  `json:"size"`
			Dir  bool   `json:"dir"`
		}

		var files []FileEntry
		for _, f := range zr.File {
			files = append(files, FileEntry{
				Path: f.Name,
				Size: int64(f.UncompressedSize64),
				Dir:  f.FileInfo().IsDir(),
			})
		}

		writeJSON(w, http.StatusOK, files)
	}
}

// GET /student/work/download?run_id=...&student_id=...
func handleDownloadWork(db *sql.DB) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		runID := r.URL.Query().Get("run_id")
		studentID := r.URL.Query().Get("student_id")
		if runID == "" || studentID == "" {
			writeError(w, http.StatusBadRequest, "run_id and student_id required")
			return
		}

		dir := filepath.Join(studentWorkDir, runID, studentID)
		entries, _ := os.ReadDir(dir)
		if len(entries) == 0 {
			writeError(w, http.StatusNotFound, "no work uploaded")
			return
		}

		zipPath := filepath.Join(dir, entries[0].Name())

		var studentName string
		db.QueryRow("SELECT name FROM registered_students WHERE id = ? AND run_id = ?", studentID, runID).Scan(&studentName)
		if studentName == "" {
			studentName = studentID
		}

		filename := studentName + ".zip"
		w.Header().Set("Content-Type", "application/zip")
		w.Header().Set("Content-Disposition", "attachment; filename=\""+filename+"\"")
		http.ServeFile(w, r, zipPath)
	}
}

// GET /student/work/file?run_id=...&student_id=...&path=...
func handleGetWorkFile(db *sql.DB) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		runID := r.URL.Query().Get("run_id")
		studentID := r.URL.Query().Get("student_id")
		filePath := r.URL.Query().Get("path")
		if runID == "" || studentID == "" || filePath == "" {
			writeError(w, http.StatusBadRequest, "run_id, student_id, and path required")
			return
		}

		if strings.Contains(filePath, "..") {
			writeError(w, http.StatusBadRequest, "invalid path")
			return
		}

		dir := filepath.Join(studentWorkDir, runID, studentID)
		entries, _ := os.ReadDir(dir)
		if len(entries) == 0 {
			writeError(w, http.StatusNotFound, "no work uploaded")
			return
		}

		zipPath := filepath.Join(dir, entries[0].Name())
		zr, err := zip.OpenReader(zipPath)
		if err != nil {
			writeError(w, http.StatusInternalServerError, "failed to read zip")
			return
		}
		defer zr.Close()

		for _, f := range zr.File {
			if f.Name == filePath {
				if f.FileInfo().IsDir() {
					writeError(w, http.StatusBadRequest, "path is a directory")
					return
				}
				rc, err := f.Open()
				if err != nil {
					writeError(w, http.StatusInternalServerError, "failed to open file in zip")
					return
				}
				defer rc.Close()

				ext := strings.ToLower(filepath.Ext(filePath))
				switch ext {
				case ".html", ".htm":
					w.Header().Set("Content-Type", "text/html; charset=utf-8")
				case ".css":
					w.Header().Set("Content-Type", "text/css; charset=utf-8")
				case ".js":
					w.Header().Set("Content-Type", "application/javascript; charset=utf-8")
				case ".json":
					w.Header().Set("Content-Type", "application/json; charset=utf-8")
				case ".png":
					w.Header().Set("Content-Type", "image/png")
				case ".jpg", ".jpeg":
					w.Header().Set("Content-Type", "image/jpeg")
				case ".pdf":
					w.Header().Set("Content-Type", "application/pdf")
				default:
					w.Header().Set("Content-Type", "text/plain; charset=utf-8")
				}
				io.Copy(w, rc)
				return
			}
		}

		writeError(w, http.StatusNotFound, "file not found in archive")
	}
}
