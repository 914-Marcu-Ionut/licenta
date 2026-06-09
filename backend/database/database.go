package database

import (
	"database/sql"
	"fmt"

	_ "modernc.org/sqlite"
)

func InitDB(path string) (*sql.DB, error) {
	db, err := sql.Open("sqlite", path)
	if err != nil {
		return nil, fmt.Errorf("open db: %w", err)
	}

	if err := db.Ping(); err != nil {
		db.Close()
		return nil, fmt.Errorf("ping db: %w", err)
	}

	if _, err := db.Exec("PRAGMA journal_mode=WAL"); err != nil {
		db.Close()
		return nil, fmt.Errorf("set WAL mode: %w", err)
	}

	if err := createTables(db); err != nil {
		db.Close()
		return nil, err
	}

	return db, nil
}

func createTables(db *sql.DB) error {
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
			return fmt.Errorf("create table: %w", err)
		}
	}
	return nil
}
