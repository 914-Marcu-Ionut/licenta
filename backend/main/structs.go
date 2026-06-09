package main

import "time"

type Settings struct {
	CertFile string `json:"cert_file"`
	KeyFile  string `json:"key_file"`
	Port     int    `json:"port"`
	DbPath   string `json:"db_path"`
	LogLevel int    `json:"log_level"` // 0 = info, 1 = debug
}

type Teacher struct {
	ID           string `json:"id"`
	Name         string `json:"name"`
	PasswordHash string `json:"password_hash,omitempty"`
}

type Exam struct {
	ID        string    `json:"id"`
	CreatedBy string    `json:"created_by"`
	Name      string    `json:"name"`
	Settings  string    `json:"settings"` // flexible JSON blob stored as text
	CreatedAt time.Time `json:"created_at"`
	Runs      []ExamRun `json:"runs,omitempty"`
}

type ExamRun struct {
	ID                 string              `json:"id"`
	ExamID             string              `json:"exam_id"`
	Status             string              `json:"status"` // "pending", "running", "finished"
	StartedAt          time.Time           `json:"started_at,omitempty"`
	EndedAt            time.Time           `json:"ended_at,omitempty"`
	RegisteredStudents []RegisteredStudent `json:"registered_students,omitempty"`
}

type RegisteredStudent struct {
	ID    string `json:"id"`
	RunID string `json:"run_id"`
	Name  string `json:"name"`
	Stats string `json:"stats"` // flexible JSON blob stored as text
}

type Event struct {
	ID        string    `json:"id"`
	StudentID string    `json:"student_id"`
	RunID     string    `json:"run_id"`
	Type      string    `json:"type"`
	Detail    string    `json:"detail"`
	CreatedAt time.Time `json:"created_at"`
}
