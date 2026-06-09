package main

import (
	"crypto/sha256"
	"database/sql"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"log"
	"strings"
	"sync"
	"time"
)

type Flag struct {
	Type      string    `json:"type"`
	Detail    string    `json:"detail"`
	Severity  string    `json:"severity"` // "low", "medium", "high"
	Timestamp time.Time `json:"timestamp"`
}

type CheatingState struct {
	mu            sync.Mutex
	StudentID     string    `json:"student_id"`
	RunID         string    `json:"run_id"`
	StudentName   string    `json:"student_name"`
	Flags         []Flag    `json:"flags"`
	LastHeartbeat time.Time `json:"last_heartbeat"`
	Connected     bool      `json:"connected"`
	Finished      bool      `json:"finished"`
	Score         int       `json:"score"`
	ExpectedSeq   int       `json:"expected_seq"`
	LastMsgHash   string    `json:"last_msg_hash"`
}

var (
	cheatingMu     sync.RWMutex
	cheatingStates = map[string]*CheatingState{} // key = student_id
)

func GetCheatingState(studentID, runID string) *CheatingState {
	cheatingMu.Lock()
	defer cheatingMu.Unlock()

	if cs, ok := cheatingStates[studentID]; ok {
		return cs
	}
	cs := &CheatingState{
		StudentID:     studentID,
		RunID:         runID,
		Flags:         []Flag{},
		LastHeartbeat: time.Now(),
		Connected:     true,
	}
	cheatingStates[studentID] = cs
	return cs
}

func (cs *CheatingState) AddFlag(flagType, detail, severity string) {
	cs.Flags = append(cs.Flags, Flag{
		Type:      flagType,
		Detail:    detail,
		Severity:  severity,
		Timestamp: time.Now().UTC(),
	})
	switch severity {
	case "high":
		cs.Score += 3
	case "medium":
		cs.Score += 2
	default:
		cs.Score++
	}
	log.Printf("[CHEAT] Flag for %s: [%s] %s — %s (score: %d)", cs.StudentID, severity, flagType, detail, cs.Score)
}

func computeHash(rawMessage []byte) string {
	h := sha256.Sum256(rawMessage)
	return hex.EncodeToString(h[:])
}

func (cs *CheatingState) VerifyChain(rawMessage []byte, seq int, prevHash string) bool {
	cs.mu.Lock()
	defer cs.mu.Unlock()

	valid := true

	if seq != cs.ExpectedSeq {
		cs.AddFlag("chain_seq_mismatch",
			fmt.Sprintf("expected seq %d, got %d — possible dropped packets", cs.ExpectedSeq, seq),
			"high")
		valid = false
	}

	if cs.ExpectedSeq > 0 && prevHash != cs.LastMsgHash {
		cs.AddFlag("chain_hash_mismatch",
			fmt.Sprintf("prev_hash mismatch at seq %d — message chain broken", seq),
			"high")
		valid = false
	}

	cs.ExpectedSeq = seq + 1
	cs.LastMsgHash = computeHash(rawMessage)

	return valid
}

func (cs *CheatingState) ResetChain() {
	cs.mu.Lock()
	defer cs.mu.Unlock()
	cs.ExpectedSeq = 0
	cs.LastMsgHash = ""
}

func (cs *CheatingState) Update(eventType string, detail json.RawMessage) {
	cs.mu.Lock()
	defer cs.mu.Unlock()

	switch eventType {

	case "heartbeat":
		cs.LastHeartbeat = time.Now()

	case "clipboard_text":
		/*
			var d struct {
				Text         string `json:"text"`
				ActiveWindow struct {
					Window  string `json:"window"`
					Process string `json:"process"`
				} `json:"active_window"`
			}
			json.Unmarshal(detail, &d)

			proc := strings.ToLower(d.ActiveWindow.Process)
			if proc != "" && !strings.Contains(proc, `\users\exam_user\`) {
				cs.AddFlag("clipboard_outside_env", "text copied from outside exam environment: "+d.ActiveWindow.Process, "high")
			}
		*/
		//don't flag it as cheating, might be a false positive

	case "clipboard_file":
		var d struct {
			Files        []string `json:"files"`
			ActiveWindow struct {
				Process string `json:"process"`
			} `json:"active_window"`
		}
		json.Unmarshal(detail, &d)

		fileList := strings.Join(d.Files, ", ")
		proc := strings.ToLower(d.ActiveWindow.Process)
		if proc != "" && !strings.Contains(proc, `\users\exam_user\`) {
			msg := "file copied from outside exam environment: " + d.ActiveWindow.Process
			if fileList != "" {
				msg += " | files: " + fileList
			}
			cs.AddFlag("file_copy_outside_env", msg, "high")
		}

	case "firewall_tampered":
		cs.AddFlag("firewall_tampered", "firewall rules were modified", "high")

	case "firewall_error":
		var d struct {
			Message string `json:"message"`
		}
		json.Unmarshal(detail, &d)
		cs.AddFlag("firewall_error", d.Message, "medium")

	case "session_logoff", "session_ending", "session_lock":
		cs.AddFlag(eventType, "user logged off during exam", "high")

	case "session_shutdown_requested":
		cs.AddFlag("shutdown_attempt", "shutdown/logoff was requested", "high")

	case "usb_connected":
		var d struct {
			Drive string `json:"drive"`
		}
		json.Unmarshal(detail, &d)
		cs.AddFlag("usb_connected", "USB device connected: "+d.Drive, "high")

	case "usb_disconnected":
		var d struct {
			Drive string `json:"drive"`
		}
		json.Unmarshal(detail, &d)
		cs.AddFlag("usb_disconnected", "USB device disconnected: "+d.Drive, "low")

	case "vm_detection":
		var d struct {
			IsVM   bool   `json:"is_vm"`
			Reason string `json:"reason"`
		}
		json.Unmarshal(detail, &d)
		if d.IsVM {
			cs.AddFlag("virtual_machine", "exam running inside VM: "+d.Reason, "high")
		}

	case "tcp_snapshot":
		// stored in events table for post-exam analysis, no automatic flagging

	default:
		if strings.HasPrefix(eventType, "session_") {
			cs.AddFlag(eventType, "session event: "+eventType, "medium")
		}
	}
}

func (cs *CheatingState) ToJSON() string {
	cs.mu.Lock()
	defer cs.mu.Unlock()
	data, _ := json.Marshal(cs)
	return string(data)
}

func (cs *CheatingState) ToString() string {
	cs.mu.Lock()
	defer cs.mu.Unlock()
	data, _ := json.MarshalIndent(cs, "", "  ")
	return string(data)
}

func (cs *CheatingState) SaveToDB(db *sql.DB) {
	statsJSON := cs.ToJSON()
	_, err := db.Exec(
		"UPDATE registered_students SET stats = ? WHERE id = ?",
		statsJSON, cs.StudentID,
	)
	if err != nil {
		log.Printf("[CHEAT] Failed to save stats for %s: %v", cs.StudentID, err)
	}
}

func StartHeartbeatChecker(db *sql.DB) {
	go func() {
		for {
			time.Sleep(5 * time.Second)

			cheatingMu.RLock()
			states := make([]*CheatingState, 0, len(cheatingStates))
			for _, cs := range cheatingStates {
				states = append(states, cs)
			}
			cheatingMu.RUnlock()

			now := time.Now()
			for _, cs := range states {
				cs.mu.Lock()
				if cs.Finished {
					cs.mu.Unlock()
					continue
				}
				if cs.Connected && now.Sub(cs.LastHeartbeat) > 10*time.Second {
					cs.AddFlag("student_disconnected", "no heartbeat for 10+ seconds — student offline", "medium")
					cs.Connected = false
					name := cs.StudentName
					cs.mu.Unlock()
					cs.SaveToDB(db)

					notifyTeachersFlag(cs.RunID, cs.StudentID, "student_disconnected", "no heartbeat for 10+ seconds — student offline", "medium")
					broadcastStudentStatus(cs.RunID, cs.StudentID, name, false)
				} else {
					cs.mu.Unlock()
				}
			}
		}
	}()
}

func StartExamExpirationChecker(db *sql.DB) {
	go func() {
		for {
			time.Sleep(10 * time.Second)

			rows, err := db.Query(`
				SELECT er.id, e.settings, er.started_at
				FROM exam_runs er
				JOIN exams e ON e.id = er.exam_id
				WHERE er.status = 'running' AND er.started_at IS NOT NULL
			`)
			if err != nil {
				log.Printf("[EXPIRY] query error: %v", err)
				continue
			}

			type expiredRun struct {
				ID string
			}
			var expired []expiredRun

			for rows.Next() {
				var runID, settings string
				var startedAt time.Time
				if err := rows.Scan(&runID, &settings, &startedAt); err != nil {
					continue
				}

				var s struct {
					ExamDuration int `json:"exam_duration"`
				}
				json.Unmarshal([]byte(settings), &s)
				if s.ExamDuration <= 0 {
					continue
				}

				endsAt := startedAt.Add(time.Duration(s.ExamDuration) * time.Minute)
				if time.Now().UTC().After(endsAt) {
					expired = append(expired, expiredRun{ID: runID})
				}
			}
			rows.Close()

			for _, run := range expired {
				now := time.Now().UTC()
				db.Exec("UPDATE exam_runs SET status = 'finished', ended_at = ? WHERE id = ?", now, run.ID)
				log.Printf("[EXPIRY] auto-closed exam run %s (duration expired)", run.ID)
				notifyRunEnded(run.ID)
			}
		}
	}()
}

func notifyRunEnded(runID string) {
	clientsMu.RLock()
	defer clientsMu.RUnlock()

	for c := range clients {
		if c.RunID == runID {
			c.Send(WSResponse{
				ID:      0,
				Status:  0,
				Message: "exam_ended",
				Data: map[string]string{
					"run_id": runID,
					"reason": "duration_expired",
				},
			})
			if c.Role == "student" {
				c.Finished = true
				if c.Cheating != nil {
					c.Cheating.mu.Lock()
					c.Cheating.Finished = true
					c.Cheating.mu.Unlock()
				}
			}
		}
	}
}

func broadcastStudentStatus(runID, studentID, studentName string, online bool) {
	clientsMu.RLock()
	defer clientsMu.RUnlock()

	status := "offline"
	if online {
		status = "online"
	}

	for c := range clients {
		if c.Role == "teacher" && c.RunID == runID {
			c.Send(WSResponse{
				ID:      0,
				Status:  0,
				Message: "student_status",
				Data: map[string]string{
					"student_name": studentName,
					"student_id":   studentID,
					"status":       status,
				},
			})
		}
	}
}

func notifyTeachersFlag(runID, studentID, flagType, detail, severity string) {
	cheatingMu.RLock()
	var studentName string
	if cs, ok := cheatingStates[studentID]; ok {
		cs.mu.Lock()
		studentName = cs.StudentName
		cs.mu.Unlock()
	}
	cheatingMu.RUnlock()

	clientsMu.RLock()
	defer clientsMu.RUnlock()

	for c := range clients {
		if c.Role == "teacher" && c.RunID == runID {
			c.Send(WSResponse{
				ID:      0,
				Status:  0,
				Message: "student_flag",
				Data: map[string]string{
					"student_name": studentName,
					"student_id":   studentID,
					"flag":         flagType,
					"detail":       detail,
					"severity":     severity,
				},
			})
		}
	}
}
