package main

import (
	"bytes"
	"database/sql"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"testing"

	_ "modernc.org/sqlite"
)

func setupTestDB(t *testing.T) *sql.DB {
	t.Helper()
	db, err := sql.Open("sqlite", ":memory:")
	if err != nil {
		t.Fatalf("open test db: %v", err)
	}

	tables := []string{
		`CREATE TABLE IF NOT EXISTS teachers (
			id            TEXT PRIMARY KEY,
			name          TEXT NOT NULL,
			password_hash TEXT NOT NULL
		)`,
		`CREATE TABLE IF NOT EXISTS exams (
			id         TEXT PRIMARY KEY,
			created_by TEXT NOT NULL REFERENCES teachers(id),
			name       TEXT NOT NULL,
			settings   TEXT NOT NULL DEFAULT '{}',
			created_at DATETIME NOT NULL
		)`,
		`CREATE TABLE IF NOT EXISTS exam_runs (
			id         TEXT PRIMARY KEY,
			exam_id    TEXT NOT NULL REFERENCES exams(id),
			status     TEXT NOT NULL DEFAULT 'pending',
			started_at DATETIME,
			ended_at   DATETIME
		)`,
		`CREATE TABLE IF NOT EXISTS registered_students (
			id     TEXT PRIMARY KEY,
			run_id TEXT NOT NULL REFERENCES exam_runs(id),
			name   TEXT NOT NULL,
			stats  TEXT NOT NULL DEFAULT '{}',
			UNIQUE(run_id, name)
		)`,
		`CREATE TABLE IF NOT EXISTS events (
			id         TEXT PRIMARY KEY,
			student_id TEXT NOT NULL REFERENCES registered_students(id),
			run_id     TEXT NOT NULL,
			type       TEXT NOT NULL,
			detail     TEXT NOT NULL DEFAULT '{}',
			created_at DATETIME NOT NULL
		)`,
	}

	for _, ddl := range tables {
		if _, err := db.Exec(ddl); err != nil {
			t.Fatalf("create table: %v", err)
		}
	}

	t.Cleanup(func() { db.Close() })
	return db
}

func jsonBody(t *testing.T, v any) *bytes.Buffer {
	t.Helper()
	buf := &bytes.Buffer{}
	if err := json.NewEncoder(buf).Encode(v); err != nil {
		t.Fatalf("encode json body: %v", err)
	}
	return buf
}

func parseJSON(t *testing.T, rec *httptest.ResponseRecorder, dest any) {
	t.Helper()
	if err := json.NewDecoder(rec.Body).Decode(dest); err != nil {
		t.Fatalf("decode response: %v (body: %s)", err, rec.Body.String())
	}
}

// registerTeacher is a helper that creates a teacher and returns the ID.
func registerTeacher(t *testing.T, db *sql.DB, name, password string) string {
	t.Helper()
	body := jsonBody(t, map[string]string{"name": name, "password": password})
	req := httptest.NewRequest(http.MethodPost, "/register", body)
	rec := httptest.NewRecorder()
	handleRegister(db).ServeHTTP(rec, req)

	if rec.Code != http.StatusCreated {
		t.Fatalf("register teacher: expected 201, got %d: %s", rec.Code, rec.Body.String())
	}

	var resp Teacher
	parseJSON(t, rec, &resp)
	return resp.ID
}

// createExam is a helper that creates an exam and returns the ID.
func createExam(t *testing.T, db *sql.DB, teacherID, name string) string {
	t.Helper()
	body := jsonBody(t, map[string]string{
		"created_by": teacherID,
		"name":       name,
	})
	req := httptest.NewRequest(http.MethodPost, "/exam", body)
	rec := httptest.NewRecorder()
	handleCreateExam(db).ServeHTTP(rec, req)

	if rec.Code != http.StatusCreated {
		t.Fatalf("create exam: expected 201, got %d: %s", rec.Code, rec.Body.String())
	}

	var resp Exam
	parseJSON(t, rec, &resp)
	return resp.ID
}

// createExamRun is a helper that creates a run and returns the ID.
func createExamRun(t *testing.T, db *sql.DB, examID string) string {
	t.Helper()
	body := jsonBody(t, map[string]string{"exam_id": examID})
	req := httptest.NewRequest(http.MethodPost, "/exam/run", body)
	rec := httptest.NewRecorder()
	handleCreateExamRun(db).ServeHTTP(rec, req)

	if rec.Code != http.StatusCreated {
		t.Fatalf("create exam run: expected 201, got %d: %s", rec.Code, rec.Body.String())
	}

	var resp ExamRun
	parseJSON(t, rec, &resp)
	return resp.ID
}

// --- Auth Tests ---

func TestRegister_Success(t *testing.T) {
	db := setupTestDB(t)

	body := jsonBody(t, map[string]string{"name": "prof1", "password": "secret123"})
	req := httptest.NewRequest(http.MethodPost, "/register", body)
	rec := httptest.NewRecorder()

	handleRegister(db).ServeHTTP(rec, req)

	if rec.Code != http.StatusCreated {
		t.Fatalf("expected 201, got %d", rec.Code)
	}

	var resp Teacher
	parseJSON(t, rec, &resp)

	if resp.Name != "prof1" {
		t.Errorf("expected name 'prof1', got %q", resp.Name)
	}
	if resp.ID == "" {
		t.Error("expected non-empty ID")
	}
}

func TestRegister_MissingFields(t *testing.T) {
	db := setupTestDB(t)

	tests := []struct {
		name string
		body map[string]string
	}{
		{"missing password", map[string]string{"name": "prof1"}},
		{"missing name", map[string]string{"password": "secret123"}},
		{"both empty", map[string]string{"name": "", "password": ""}},
	}

	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			body := jsonBody(t, tc.body)
			req := httptest.NewRequest(http.MethodPost, "/register", body)
			rec := httptest.NewRecorder()

			handleRegister(db).ServeHTTP(rec, req)

			if rec.Code != http.StatusBadRequest {
				t.Errorf("expected 400, got %d", rec.Code)
			}
		})
	}
}

func TestRegister_InvalidJSON(t *testing.T) {
	db := setupTestDB(t)

	req := httptest.NewRequest(http.MethodPost, "/register", bytes.NewBufferString("not json"))
	rec := httptest.NewRecorder()

	handleRegister(db).ServeHTTP(rec, req)

	if rec.Code != http.StatusBadRequest {
		t.Errorf("expected 400, got %d", rec.Code)
	}
}

func TestLogin_Success(t *testing.T) {
	db := setupTestDB(t)
	registerTeacher(t, db, "prof1", "secret123")

	body := jsonBody(t, map[string]string{"name": "prof1", "password": "secret123"})
	req := httptest.NewRequest(http.MethodPost, "/login", body)
	rec := httptest.NewRecorder()

	handleLogin(db).ServeHTTP(rec, req)

	if rec.Code != http.StatusOK {
		t.Fatalf("expected 200, got %d: %s", rec.Code, rec.Body.String())
	}

	var resp Teacher
	parseJSON(t, rec, &resp)

	if resp.Name != "prof1" {
		t.Errorf("expected name 'prof1', got %q", resp.Name)
	}
	if resp.PasswordHash != "" {
		t.Error("password_hash should not be returned")
	}
}

func TestLogin_WrongPassword(t *testing.T) {
	db := setupTestDB(t)
	registerTeacher(t, db, "prof1", "secret123")

	body := jsonBody(t, map[string]string{"name": "prof1", "password": "wrong"})
	req := httptest.NewRequest(http.MethodPost, "/login", body)
	rec := httptest.NewRecorder()

	handleLogin(db).ServeHTTP(rec, req)

	if rec.Code != http.StatusUnauthorized {
		t.Errorf("expected 401, got %d", rec.Code)
	}
}

func TestLogin_NonexistentUser(t *testing.T) {
	db := setupTestDB(t)

	body := jsonBody(t, map[string]string{"name": "nobody", "password": "secret123"})
	req := httptest.NewRequest(http.MethodPost, "/login", body)
	rec := httptest.NewRecorder()

	handleLogin(db).ServeHTTP(rec, req)

	if rec.Code != http.StatusUnauthorized {
		t.Errorf("expected 401, got %d", rec.Code)
	}
}

func TestLogin_MissingFields(t *testing.T) {
	db := setupTestDB(t)

	body := jsonBody(t, map[string]string{"name": "prof1"})
	req := httptest.NewRequest(http.MethodPost, "/login", body)
	rec := httptest.NewRecorder()

	handleLogin(db).ServeHTTP(rec, req)

	if rec.Code != http.StatusBadRequest {
		t.Errorf("expected 400, got %d", rec.Code)
	}
}

// --- Exam CRUD Tests ---

func TestCreateExam_Success(t *testing.T) {
	db := setupTestDB(t)
	teacherID := registerTeacher(t, db, "prof1", "pass")

	body := jsonBody(t, map[string]string{
		"created_by": teacherID,
		"name":       "Math Final",
		"settings":   `{"duration":60}`,
	})
	req := httptest.NewRequest(http.MethodPost, "/exam", body)
	rec := httptest.NewRecorder()

	handleCreateExam(db).ServeHTTP(rec, req)

	if rec.Code != http.StatusCreated {
		t.Fatalf("expected 201, got %d: %s", rec.Code, rec.Body.String())
	}

	var resp Exam
	parseJSON(t, rec, &resp)

	if resp.Name != "Math Final" {
		t.Errorf("expected name 'Math Final', got %q", resp.Name)
	}
	if resp.CreatedBy != teacherID {
		t.Errorf("expected created_by %q, got %q", teacherID, resp.CreatedBy)
	}
	if resp.Settings != `{"duration":60}` {
		t.Errorf("expected settings preserved, got %q", resp.Settings)
	}
}

func TestCreateExam_InvalidTeacher(t *testing.T) {
	db := setupTestDB(t)

	body := jsonBody(t, map[string]string{
		"created_by": "nonexistent",
		"name":       "Math Final",
	})
	req := httptest.NewRequest(http.MethodPost, "/exam", body)
	rec := httptest.NewRecorder()

	handleCreateExam(db).ServeHTTP(rec, req)

	if rec.Code != http.StatusBadRequest {
		t.Errorf("expected 400, got %d", rec.Code)
	}
}

func TestCreateExam_MissingFields(t *testing.T) {
	db := setupTestDB(t)
	teacherID := registerTeacher(t, db, "prof1", "pass")

	tests := []struct {
		name string
		body map[string]string
	}{
		{"missing name", map[string]string{"created_by": teacherID}},
		{"missing created_by", map[string]string{"name": "Math"}},
	}

	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			body := jsonBody(t, tc.body)
			req := httptest.NewRequest(http.MethodPost, "/exam", body)
			rec := httptest.NewRecorder()

			handleCreateExam(db).ServeHTTP(rec, req)

			if rec.Code != http.StatusBadRequest {
				t.Errorf("expected 400, got %d", rec.Code)
			}
		})
	}
}

func TestUpdateExam_Success(t *testing.T) {
	db := setupTestDB(t)
	teacherID := registerTeacher(t, db, "prof1", "pass")
	examID := createExam(t, db, teacherID, "Math")

	body := jsonBody(t, map[string]string{
		"id":       examID,
		"name":     "Math Updated",
		"settings": `{"duration":90}`,
	})
	req := httptest.NewRequest(http.MethodPut, "/exam", body)
	rec := httptest.NewRecorder()

	handleUpdateExam(db).ServeHTTP(rec, req)

	if rec.Code != http.StatusOK {
		t.Fatalf("expected 200, got %d: %s", rec.Code, rec.Body.String())
	}

	var resp Exam
	parseJSON(t, rec, &resp)

	if resp.Name != "Math Updated" {
		t.Errorf("expected name 'Math Updated', got %q", resp.Name)
	}
	if resp.Settings != `{"duration":90}` {
		t.Errorf("expected updated settings, got %q", resp.Settings)
	}
}

func TestUpdateExam_NotFound(t *testing.T) {
	db := setupTestDB(t)

	body := jsonBody(t, map[string]string{"id": "nonexistent", "name": "X"})
	req := httptest.NewRequest(http.MethodPut, "/exam", body)
	rec := httptest.NewRecorder()

	handleUpdateExam(db).ServeHTTP(rec, req)

	if rec.Code != http.StatusNotFound {
		t.Errorf("expected 404, got %d", rec.Code)
	}
}

func TestUpdateExam_ConflictWhileRunning(t *testing.T) {
	db := setupTestDB(t)
	teacherID := registerTeacher(t, db, "prof1", "pass")
	examID := createExam(t, db, teacherID, "Math")
	runID := createExamRun(t, db, examID)

	// Set run to "running"
	db.Exec("UPDATE exam_runs SET status = 'running' WHERE id = ?", runID)

	body := jsonBody(t, map[string]string{"id": examID, "name": "New Name"})
	req := httptest.NewRequest(http.MethodPut, "/exam", body)
	rec := httptest.NewRecorder()

	handleUpdateExam(db).ServeHTTP(rec, req)

	if rec.Code != http.StatusConflict {
		t.Errorf("expected 409, got %d", rec.Code)
	}
}

func TestDeleteExam_Success(t *testing.T) {
	db := setupTestDB(t)
	teacherID := registerTeacher(t, db, "prof1", "pass")
	examID := createExam(t, db, teacherID, "Math")

	req := httptest.NewRequest(http.MethodDelete, "/exam?id="+examID, nil)
	rec := httptest.NewRecorder()

	handleDeleteExam(db).ServeHTTP(rec, req)

	if rec.Code != http.StatusOK {
		t.Fatalf("expected 200, got %d: %s", rec.Code, rec.Body.String())
	}

	// Verify it's gone
	var exists bool
	db.QueryRow("SELECT 1 FROM exams WHERE id = ?", examID).Scan(&exists)
	if exists {
		t.Error("exam should have been deleted")
	}
}

func TestDeleteExam_NotFound(t *testing.T) {
	db := setupTestDB(t)

	req := httptest.NewRequest(http.MethodDelete, "/exam?id=nonexistent", nil)
	rec := httptest.NewRecorder()

	handleDeleteExam(db).ServeHTTP(rec, req)

	if rec.Code != http.StatusNotFound {
		t.Errorf("expected 404, got %d", rec.Code)
	}
}

func TestDeleteExam_MissingID(t *testing.T) {
	db := setupTestDB(t)

	req := httptest.NewRequest(http.MethodDelete, "/exam", nil)
	rec := httptest.NewRecorder()

	handleDeleteExam(db).ServeHTTP(rec, req)

	if rec.Code != http.StatusBadRequest {
		t.Errorf("expected 400, got %d", rec.Code)
	}
}

func TestListExams_Empty(t *testing.T) {
	db := setupTestDB(t)

	req := httptest.NewRequest(http.MethodGet, "/exams", nil)
	rec := httptest.NewRecorder()

	handleListExams(db).ServeHTTP(rec, req)

	if rec.Code != http.StatusOK {
		t.Fatalf("expected 200, got %d", rec.Code)
	}

	var exams []Exam
	parseJSON(t, rec, &exams)

	if len(exams) != 0 {
		t.Errorf("expected empty list, got %d exams", len(exams))
	}
}

func TestListExams_WithData(t *testing.T) {
	db := setupTestDB(t)
	teacherID := registerTeacher(t, db, "prof1", "pass")
	createExam(t, db, teacherID, "Math")
	createExam(t, db, teacherID, "Physics")

	req := httptest.NewRequest(http.MethodGet, "/exams", nil)
	rec := httptest.NewRecorder()

	handleListExams(db).ServeHTTP(rec, req)

	if rec.Code != http.StatusOK {
		t.Fatalf("expected 200, got %d", rec.Code)
	}

	var exams []Exam
	parseJSON(t, rec, &exams)

	if len(exams) != 2 {
		t.Errorf("expected 2 exams, got %d", len(exams))
	}
}

func TestGetExam_Success(t *testing.T) {
	db := setupTestDB(t)
	teacherID := registerTeacher(t, db, "prof1", "pass")
	examID := createExam(t, db, teacherID, "Math")

	req := httptest.NewRequest(http.MethodGet, "/exam?id="+examID, nil)
	rec := httptest.NewRecorder()

	handleGetExam(db).ServeHTTP(rec, req)

	if rec.Code != http.StatusOK {
		t.Fatalf("expected 200, got %d: %s", rec.Code, rec.Body.String())
	}

	var resp Exam
	parseJSON(t, rec, &resp)

	if resp.Name != "Math" {
		t.Errorf("expected name 'Math', got %q", resp.Name)
	}
	if resp.ID != examID {
		t.Errorf("expected id %q, got %q", examID, resp.ID)
	}
}

func TestGetExam_NotFound(t *testing.T) {
	db := setupTestDB(t)

	req := httptest.NewRequest(http.MethodGet, "/exam?id=nonexistent", nil)
	rec := httptest.NewRecorder()

	handleGetExam(db).ServeHTTP(rec, req)

	if rec.Code != http.StatusNotFound {
		t.Errorf("expected 404, got %d", rec.Code)
	}
}

// --- Exam Run Tests ---

func TestCreateExamRun_Success(t *testing.T) {
	db := setupTestDB(t)
	teacherID := registerTeacher(t, db, "prof1", "pass")
	examID := createExam(t, db, teacherID, "Math")

	body := jsonBody(t, map[string]string{"exam_id": examID})
	req := httptest.NewRequest(http.MethodPost, "/exam/run", body)
	rec := httptest.NewRecorder()

	handleCreateExamRun(db).ServeHTTP(rec, req)

	if rec.Code != http.StatusCreated {
		t.Fatalf("expected 201, got %d: %s", rec.Code, rec.Body.String())
	}

	var resp ExamRun
	parseJSON(t, rec, &resp)

	if resp.ExamID != examID {
		t.Errorf("expected exam_id %q, got %q", examID, resp.ExamID)
	}
	if resp.Status != "pending" {
		t.Errorf("expected status 'pending', got %q", resp.Status)
	}
}

func TestCreateExamRun_ExamNotFound(t *testing.T) {
	db := setupTestDB(t)

	body := jsonBody(t, map[string]string{"exam_id": "nonexistent"})
	req := httptest.NewRequest(http.MethodPost, "/exam/run", body)
	rec := httptest.NewRecorder()

	handleCreateExamRun(db).ServeHTTP(rec, req)

	if rec.Code != http.StatusBadRequest {
		t.Errorf("expected 400, got %d", rec.Code)
	}
}

func TestUpdateRunStatus_ToRunning(t *testing.T) {
	db := setupTestDB(t)
	teacherID := registerTeacher(t, db, "prof1", "pass")
	examID := createExam(t, db, teacherID, "Math")
	runID := createExamRun(t, db, examID)

	body := jsonBody(t, map[string]string{"run_id": runID, "status": "running"})
	req := httptest.NewRequest(http.MethodPut, "/exam/run/status", body)
	rec := httptest.NewRecorder()

	handleUpdateRunStatus(db).ServeHTTP(rec, req)

	if rec.Code != http.StatusOK {
		t.Fatalf("expected 200, got %d: %s", rec.Code, rec.Body.String())
	}

	// Verify DB state
	var status string
	db.QueryRow("SELECT status FROM exam_runs WHERE id = ?", runID).Scan(&status)
	if status != "running" {
		t.Errorf("expected 'running' in DB, got %q", status)
	}
}

func TestUpdateRunStatus_InvalidStatus(t *testing.T) {
	db := setupTestDB(t)
	teacherID := registerTeacher(t, db, "prof1", "pass")
	examID := createExam(t, db, teacherID, "Math")
	runID := createExamRun(t, db, examID)

	body := jsonBody(t, map[string]string{"run_id": runID, "status": "invalid"})
	req := httptest.NewRequest(http.MethodPut, "/exam/run/status", body)
	rec := httptest.NewRecorder()

	handleUpdateRunStatus(db).ServeHTTP(rec, req)

	if rec.Code != http.StatusBadRequest {
		t.Errorf("expected 400, got %d", rec.Code)
	}
}

func TestUpdateRunStatus_RunNotFound(t *testing.T) {
	db := setupTestDB(t)

	body := jsonBody(t, map[string]string{"run_id": "nonexistent", "status": "running"})
	req := httptest.NewRequest(http.MethodPut, "/exam/run/status", body)
	rec := httptest.NewRecorder()

	handleUpdateRunStatus(db).ServeHTTP(rec, req)

	if rec.Code != http.StatusNotFound {
		t.Errorf("expected 404, got %d", rec.Code)
	}
}

func TestDeleteExamRun_Success(t *testing.T) {
	db := setupTestDB(t)
	teacherID := registerTeacher(t, db, "prof1", "pass")
	examID := createExam(t, db, teacherID, "Math")
	runID := createExamRun(t, db, examID)

	req := httptest.NewRequest(http.MethodDelete, "/exam/run?id="+runID, nil)
	rec := httptest.NewRecorder()

	handleDeleteExamRun(db).ServeHTTP(rec, req)

	if rec.Code != http.StatusOK {
		t.Fatalf("expected 200, got %d: %s", rec.Code, rec.Body.String())
	}

	var exists bool
	db.QueryRow("SELECT 1 FROM exam_runs WHERE id = ?", runID).Scan(&exists)
	if exists {
		t.Error("run should have been deleted")
	}
}

func TestDeleteExamRun_NotFound(t *testing.T) {
	db := setupTestDB(t)

	req := httptest.NewRequest(http.MethodDelete, "/exam/run?id=nonexistent", nil)
	rec := httptest.NewRecorder()

	handleDeleteExamRun(db).ServeHTTP(rec, req)

	if rec.Code != http.StatusNotFound {
		t.Errorf("expected 404, got %d", rec.Code)
	}
}

// --- Student Registration Tests ---

func TestRegisterStudent_Success(t *testing.T) {
	db := setupTestDB(t)
	teacherID := registerTeacher(t, db, "prof1", "pass")
	examID := createExam(t, db, teacherID, "Math")
	runID := createExamRun(t, db, examID)

	body := jsonBody(t, map[string]string{"run_id": runID, "name": "Student1"})
	req := httptest.NewRequest(http.MethodPost, "/exam/run/register", body)
	rec := httptest.NewRecorder()

	handleRegisterStudent(db).ServeHTTP(rec, req)

	if rec.Code != http.StatusCreated {
		t.Fatalf("expected 201, got %d: %s", rec.Code, rec.Body.String())
	}

	var resp RegisteredStudent
	parseJSON(t, rec, &resp)

	if resp.Name != "Student1" {
		t.Errorf("expected name 'Student1', got %q", resp.Name)
	}
	if resp.RunID != runID {
		t.Errorf("expected run_id %q, got %q", runID, resp.RunID)
	}
}

func TestRegisterStudent_DuplicateReturnsExisting(t *testing.T) {
	db := setupTestDB(t)
	teacherID := registerTeacher(t, db, "prof1", "pass")
	examID := createExam(t, db, teacherID, "Math")
	runID := createExamRun(t, db, examID)

	// First registration
	body := jsonBody(t, map[string]string{"run_id": runID, "name": "Student1"})
	req := httptest.NewRequest(http.MethodPost, "/exam/run/register", body)
	rec := httptest.NewRecorder()
	handleRegisterStudent(db).ServeHTTP(rec, req)

	var first RegisteredStudent
	parseJSON(t, rec, &first)

	// Second registration with same name
	body = jsonBody(t, map[string]string{"run_id": runID, "name": "Student1"})
	req = httptest.NewRequest(http.MethodPost, "/exam/run/register", body)
	rec = httptest.NewRecorder()
	handleRegisterStudent(db).ServeHTTP(rec, req)

	if rec.Code != http.StatusOK {
		t.Fatalf("expected 200 for duplicate, got %d", rec.Code)
	}

	var second RegisteredStudent
	parseJSON(t, rec, &second)

	if first.ID != second.ID {
		t.Errorf("duplicate registration should return same ID: %q vs %q", first.ID, second.ID)
	}
}

func TestRegisterStudent_RunNotFound(t *testing.T) {
	db := setupTestDB(t)

	body := jsonBody(t, map[string]string{"run_id": "nonexistent", "name": "Student1"})
	req := httptest.NewRequest(http.MethodPost, "/exam/run/register", body)
	rec := httptest.NewRecorder()

	handleRegisterStudent(db).ServeHTTP(rec, req)

	if rec.Code != http.StatusNotFound {
		t.Errorf("expected 404, got %d", rec.Code)
	}
}

func TestRegisterStudent_MissingFields(t *testing.T) {
	db := setupTestDB(t)

	tests := []struct {
		name string
		body map[string]string
	}{
		{"missing name", map[string]string{"run_id": "some-id"}},
		{"missing run_id", map[string]string{"name": "Student1"}},
	}

	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			body := jsonBody(t, tc.body)
			req := httptest.NewRequest(http.MethodPost, "/exam/run/register", body)
			rec := httptest.NewRecorder()

			handleRegisterStudent(db).ServeHTTP(rec, req)

			if rec.Code != http.StatusBadRequest {
				t.Errorf("expected 400, got %d", rec.Code)
			}
		})
	}
}

// --- Student Events Tests ---

func TestGetStudentEvents_Empty(t *testing.T) {
	db := setupTestDB(t)
	teacherID := registerTeacher(t, db, "prof1", "pass")
	examID := createExam(t, db, teacherID, "Math")
	runID := createExamRun(t, db, examID)

	// Register a student
	body := jsonBody(t, map[string]string{"run_id": runID, "name": "Student1"})
	req := httptest.NewRequest(http.MethodPost, "/exam/run/register", body)
	rec := httptest.NewRecorder()
	handleRegisterStudent(db).ServeHTTP(rec, req)
	var student RegisteredStudent
	parseJSON(t, rec, &student)

	// Get events (should be empty)
	req = httptest.NewRequest(http.MethodGet, "/student/events?id="+student.ID, nil)
	rec = httptest.NewRecorder()

	handleGetStudentEvents(db).ServeHTTP(rec, req)

	if rec.Code != http.StatusOK {
		t.Fatalf("expected 200, got %d", rec.Code)
	}

	var events []Event
	parseJSON(t, rec, &events)

	if len(events) != 0 {
		t.Errorf("expected 0 events, got %d", len(events))
	}
}

func TestGetStudentEvents_MissingID(t *testing.T) {
	db := setupTestDB(t)

	req := httptest.NewRequest(http.MethodGet, "/student/events", nil)
	rec := httptest.NewRecorder()

	handleGetStudentEvents(db).ServeHTTP(rec, req)

	if rec.Code != http.StatusBadRequest {
		t.Errorf("expected 400, got %d", rec.Code)
	}
}
