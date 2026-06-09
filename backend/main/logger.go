package main

import "log"

var debugMode bool

func initLogger(level int) {
	debugMode = level >= 1
	if debugMode {
		log.Println("[LOG] Debug logging enabled")
	}
}

func logDebug(format string, args ...any) {
	if debugMode {
		log.Printf("[DEBUG] "+format, args...)
	}
}
