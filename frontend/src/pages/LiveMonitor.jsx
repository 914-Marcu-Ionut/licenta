import { useState, useEffect, useRef } from 'react'
import { useParams, useNavigate } from 'react-router-dom'
import { createExamSocket, api } from '../api'
import EventLog from '../components/EventLog'
import StudentGrid from '../components/StudentGrid'

export default function LiveMonitor({ teacher, password }) {
  const { runId } = useParams()
  const navigate = useNavigate()
  const [status, setStatus] = useState('disconnected')
  const [events, setEvents] = useState([])
  const [studentFlags, setStudentFlags] = useState({})
  const [showHeartbeat, setShowHeartbeat] = useState(false)
  const [runInfo, setRunInfo] = useState(null)
  const [examEnded, setExamEnded] = useState(false)
  const [remainingSeconds, setRemainingSeconds] = useState(null)
  const [examEndsAt, setExamEndsAt] = useState(null)
  const wsRef = useRef(null)

  useEffect(() => {
    api('GET', `/exam/run?id=${runId}`).then(r => {
      if (r.ok) {
        setRunInfo(r.data)
        if (r.data.status === 'finished') setExamEnded(true)

        if (r.data.started_at) {
          try {
            const settings = JSON.parse(r.data.exam_settings || '{}')
            if (settings.exam_duration) {
              const endsAt = new Date(new Date(r.data.started_at).getTime() + settings.exam_duration * 60000)
              setExamEndsAt(endsAt)
            }
          } catch {}
        }

        const initial = {}
        for (const s of (r.data.registered_students || [])) {
          try {
            const stats = JSON.parse(s.stats || '{}')
            initial[s.id] = {
              name: s.name,
              flags: stats.flags || [],
              connected: stats.connected ?? false,
            }
          } catch {
            initial[s.id] = { name: s.name, flags: [], connected: false }
          }
        }
        setStudentFlags(initial)
      }
    })
  }, [runId])

  // Countdown timer
  useEffect(() => {
    if (!examEndsAt) return
    function tick() {
      const remaining = Math.max(0, Math.floor((examEndsAt.getTime() - Date.now()) / 1000))
      setRemainingSeconds(remaining)
      if (remaining <= 0) setExamEnded(true)
    }
    tick()
    const interval = setInterval(tick, 1000)
    return () => clearInterval(interval)
  }, [examEndsAt])

  useEffect(() => {
    const ws = createExamSocket(runId, teacher.name, password, {
      onStatus: (s) => setStatus(s),
      onMessage: (msg) => {
        if (msg.message === 'exam_ended') {
          setExamEnded(true)
          setRemainingSeconds(0)
          return
        }

        if (msg.data?.remaining_seconds !== undefined && msg.data?.exam_ends_at) {
          setExamEndsAt(new Date(msg.data.exam_ends_at))
        }

        if (msg.message === 'student_event' && msg.data) {
          const d = msg.data
          setEvents((prev) => [{
            time: new Date(),
            studentName: d.student_name,
            studentId: d.student_id,
            type: d.type,
            detail: d.detail,
          }, ...prev].slice(0, 500))

          setStudentFlags((prev) => ({
            ...prev,
            [d.student_id]: {
              ...prev[d.student_id],
              name: d.student_name,
              connected: true,
              lastEvent: d.type,
            }
          }))
        }

        if (msg.message === 'student_flag' && msg.data) {
          const d = msg.data
          setEvents((prev) => [{
            time: new Date(),
            studentName: d.student_name,
            studentId: d.student_id,
            type: `FLAG: ${d.flag}`,
            detail: d.detail,
            isFlag: true,
          }, ...prev].slice(0, 500))

          setStudentFlags((prev) => {
            const current = prev[d.student_id] || { name: d.student_name, flags: [] }
            return {
              ...prev,
              [d.student_id]: {
                ...current,
                name: d.student_name,
                flags: [...(current.flags || []), { type: d.flag, detail: d.detail, severity: d.severity, time: new Date() }],
              }
            }
          })
        }

        if (msg.message === 'student_status' && msg.data) {
          const d = msg.data
          setStudentFlags((prev) => {
            const existing = prev[d.student_id] || {}
            return {
              ...prev,
              [d.student_id]: {
                ...existing,
                name: d.student_name || existing.name,
                connected: d.status === 'online',
              }
            }
          })
        }
      },
    })
    wsRef.current = ws
    return () => ws.close()
  }, [runId, teacher.name, password])

  const filteredEvents = showHeartbeat
    ? events
    : events.filter(e => e.type !== 'heartbeat')

  return (
    <div className="space-y-6">
      <div className="flex items-center justify-between">
        <div>
          <button
            onClick={() => navigate('/')}
            className="text-sm text-zinc-500 hover:text-zinc-300 transition-colors mb-2"
          >
            ← Back to Dashboard
          </button>
          <h2 className="text-xl font-semibold text-white">
            Live Monitor {runInfo?.exam_name && `— ${runInfo.exam_name}`}
          </h2>
          <p className="text-sm text-zinc-500 font-mono">Run: {runId}</p>
        </div>
        <div className="flex items-center gap-4">
          {remainingSeconds !== null && <CountdownDisplay seconds={remainingSeconds} ended={examEnded} />}
          <StatusDot status={status} />
          <label className="flex items-center gap-2 text-sm text-zinc-400 cursor-pointer">
            <input
              type="checkbox"
              checked={showHeartbeat}
              onChange={(e) => setShowHeartbeat(e.target.checked)}
              className="rounded bg-zinc-800 border-zinc-600"
            />
            Show heartbeats
          </label>
        </div>
      </div>

      {examEnded && (
        <div className="bg-red-950/50 border border-red-900 rounded-xl px-5 py-3 flex items-center gap-3">
          <span className="text-red-400 font-semibold text-sm">Exam Ended</span>
          <span className="text-xs text-zinc-400">The exam duration has expired or was manually closed.</span>
        </div>
      )}

      <StudentGrid
        students={studentFlags}
        onSelect={(id) => navigate(`/student/${id}?run=${runId}`)}
        onClearFlags={async (studentId) => {
          const r = await api('POST', '/student/clear-flags', { student_id: studentId })
          if (r.ok) {
            setStudentFlags((prev) => ({
              ...prev,
              [studentId]: { ...prev[studentId], flags: [] }
            }))
          }
        }}
      />

      <EventLog events={filteredEvents} />
    </div>
  )
}

function CountdownDisplay({ seconds, ended }) {
  const h = Math.floor(seconds / 3600)
  const m = Math.floor((seconds % 3600) / 60)
  const s = seconds % 60
  const timeStr = h > 0
    ? `${h}:${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}`
    : `${m}:${String(s).padStart(2, '0')}`

  const urgent = seconds < 300 && !ended
  const color = ended
    ? 'bg-red-950 text-red-400 border-red-900'
    : urgent
      ? 'bg-amber-950 text-amber-300 border-amber-800'
      : 'bg-zinc-900 text-white border-zinc-700'

  return (
    <div className={`px-4 py-2 rounded-xl border font-mono text-lg font-bold tracking-wider ${color} ${urgent && !ended ? 'animate-pulse' : ''}`}>
      {ended ? '0:00' : timeStr}
    </div>
  )
}

function StatusDot({ status }) {
  const color = status === 'connected' ? 'bg-emerald-500' : 'bg-red-500'
  return (
    <div className="flex items-center gap-2">
      <div className={`w-2 h-2 rounded-full ${color} animate-pulse`} />
      <span className="text-sm text-zinc-400 capitalize">{status}</span>
    </div>
  )
}
