package main

import (
	"backend/database"
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"os"
)

func loadSettings(path string) (Settings, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return Settings{}, fmt.Errorf("read %s: %w", path, err)
	}
	var s Settings
	if err := json.Unmarshal(data, &s); err != nil {
		return Settings{}, fmt.Errorf("parse %s: %w", path, err)
	}
	return s, nil
}

func main() {
	settings, err := loadSettings("settings.json")
	if err != nil {
		log.Fatalf("Failed to load settings: %v", err)
	}

	initLogger(settings.LogLevel)

	db, err := database.InitDB(settings.DbPath)
	if err != nil {
		log.Fatalf("Failed to init database: %v", err)
	}
	defer db.Close()
	log.Printf("Database opened: %s", settings.DbPath)

	StartHeartbeatChecker(db)
	StartExamExpirationChecker(db)

	mux := http.NewServeMux()

	mux.Handle("GET /", http.FileServer(http.Dir("../frontend/dist")))

	mux.HandleFunc("POST /register", handleRegister(db))
	mux.HandleFunc("POST /login", handleLogin(db))
	mux.HandleFunc("POST /exam", handleCreateExam(db))
	mux.HandleFunc("PUT /exam", handleUpdateExam(db))
	mux.HandleFunc("DELETE /exam", handleDeleteExam(db))
	mux.HandleFunc("GET /exams", handleListExams(db))
	mux.HandleFunc("GET /exam", handleGetExam(db))
	mux.HandleFunc("GET /exam/run", handleGetExamRun(db))
	mux.HandleFunc("POST /exam/run", handleCreateExamRun(db))
	mux.HandleFunc("DELETE /exam/run", handleDeleteExamRun(db))
	mux.HandleFunc("PUT /exam/run/status", handleUpdateRunStatus(db))
	mux.HandleFunc("POST /exam/run/register", handleRegisterStudent(db))
	mux.HandleFunc("POST /student/clear-flags", handleClearFlags(db))
	mux.HandleFunc("GET /student/events", handleGetStudentEvents(db))
	mux.HandleFunc("POST /exam/files", handleUploadExamFiles())
	mux.HandleFunc("POST /exam/files/delete", handleDeleteExamFiles())
	mux.HandleFunc("GET /exam/files/", handleServeExamFiles(db))
	mux.HandleFunc("POST /student/upload-work", handleUploadWork(db))
	mux.HandleFunc("GET /student/work", handleListStudentWork(db))
	mux.HandleFunc("GET /student/work/files", handleListWorkFiles(db))
	mux.HandleFunc("GET /student/work/download", handleDownloadWork(db))
	mux.HandleFunc("GET /student/work/file", handleGetWorkFile(db))
	mux.HandleFunc("GET /ws", handleWebSocket(db))

	var handler http.Handler = mux
	if debugMode {
		handler = http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			logDebug("http %s %s from %s", r.Method, r.URL.String(), r.RemoteAddr)
			mux.ServeHTTP(w, r)
		})
	}

	addr := fmt.Sprintf(":%d", settings.Port)
	log.Printf("HTTPS server listening on %s", addr)
	if err := http.ListenAndServeTLS(addr, settings.CertFile, settings.KeyFile, handler); err != nil {
		log.Fatalf("Server failed: %v", err)
	}
}
