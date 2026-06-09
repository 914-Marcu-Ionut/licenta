import { Routes, Route, Navigate } from 'react-router-dom'
import { useState } from 'react'
import Login from './pages/Login'
import Dashboard from './pages/Dashboard'
import CreateExam from './pages/CreateExam'
import EditExam from './pages/EditExam'
import LiveMonitor from './pages/LiveMonitor'
import StudentDetail from './pages/StudentDetail'
import StudentWork from './pages/StudentWork'

function loadSession() {
  try {
    const raw = localStorage.getItem('exam_session')
    if (raw) return JSON.parse(raw)
  } catch {}
  return null
}

export default function App() {
  const saved = loadSession()
  const [teacher, setTeacher] = useState(saved?.teacher || null)
  const [password, setPassword] = useState(saved?.password || '')

  function handleLogin(t, pw) {
    setTeacher(t)
    setPassword(pw)
    localStorage.setItem('exam_session', JSON.stringify({ teacher: t, password: pw }))
  }

  function handleLogout() {
    setTeacher(null)
    setPassword('')
    localStorage.removeItem('exam_session')
  }

  if (!teacher) {
    return <Login onLogin={handleLogin} />
  }

  return (
    <div className="min-h-screen bg-zinc-950 text-zinc-100">
      <nav className="border-b border-zinc-800 bg-zinc-900/80 backdrop-blur-sm sticky top-0 z-50">
        <div className="max-w-7xl mx-auto px-6 h-14 flex items-center justify-between">
          <a href="/" className="text-lg font-semibold text-white tracking-tight">
            Exam Monitor
          </a>
          <div className="flex items-center gap-4">
            <span className="text-sm text-zinc-400">
              {teacher.name}
            </span>
            <button
              onClick={handleLogout}
              className="text-sm text-zinc-500 hover:text-white transition-colors"
            >
              Logout
            </button>
          </div>
        </div>
      </nav>
      <main className="max-w-7xl mx-auto px-6 py-8">
        <Routes>
          <Route path="/" element={<Dashboard teacher={teacher} />} />
          <Route path="/create-exam" element={<CreateExam teacher={teacher} />} />
          <Route path="/edit-exam/:examId" element={<EditExam />} />
          <Route path="/monitor/:runId" element={<LiveMonitor teacher={teacher} password={password} />} />
          <Route path="/student/:studentId" element={<StudentDetail teacher={teacher} password={password} />} />
          <Route path="/work" element={<StudentWork teacher={teacher} />} />
          <Route path="*" element={<Navigate to="/" />} />
        </Routes>
      </main>
    </div>
  )
}
