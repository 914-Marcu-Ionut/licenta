import { useState, useEffect } from 'react'
import { useNavigate } from 'react-router-dom'
import { api } from '../api'

export default function Dashboard({ teacher }) {
  const [exams, setExams] = useState([])
  const navigate = useNavigate()

  useEffect(() => { loadExams() }, [])

  async function loadExams() {
    const r = await api('GET', '/exams')
    if (r.ok) setExams(r.data)
  }

  async function createRun(examId) {
    await api('POST', '/exam/run', { exam_id: examId })
    loadExams()
  }

  async function updateRunStatus(runId, status) {
    await api('PUT', '/exam/run/status', { run_id: runId, status })
    loadExams()
  }

  async function deleteExam(examId, examName) {
    if (!window.confirm(`Delete exam "${examName}" and ALL its runs, students, and files?\n\nThis cannot be undone.`)) return
    await api('DELETE', `/exam?id=${examId}`)
    loadExams()
  }

  async function deleteRun(runId) {
    if (!window.confirm(`Delete this run and all its student data and events?\n\nThis cannot be undone.`)) return
    await api('DELETE', `/exam/run?id=${runId}`)
    loadExams()
  }

  return (
    <div className="space-y-8">
      {/* Header */}
      <div className="flex items-center justify-between">
        <div>
          <h2 className="text-2xl font-bold text-white">Your Exams</h2>
          <p className="text-sm text-zinc-500 mt-1">{exams.length} exam{exams.length !== 1 ? 's' : ''} created</p>
        </div>
        <button
          onClick={() => navigate('/create-exam')}
          className="px-5 py-2.5 bg-indigo-600 hover:bg-indigo-500 text-white text-sm font-medium rounded-xl transition-all shadow-lg shadow-indigo-900/20 hover:shadow-indigo-900/40"
        >
          + New Exam
        </button>
      </div>

      {/* Empty State */}
      {exams.length === 0 && (
        <div className="text-center py-16 bg-zinc-900/50 border border-zinc-800 border-dashed rounded-xl">
          <p className="text-zinc-500 text-sm mb-3">No exams yet</p>
          <button
            onClick={() => navigate('/create-exam')}
            className="text-indigo-400 hover:text-indigo-300 text-sm font-medium transition-colors"
          >
            Create your first exam →
          </button>
        </div>
      )}

      {/* Exam List */}
      <div className="space-y-6">
        {exams.map((exam) => (
          <ExamCard
            key={exam.id}
            exam={exam}
            onCreateRun={() => createRun(exam.id)}
            onUpdateStatus={updateRunStatus}
            onDeleteExam={() => deleteExam(exam.id, exam.name)}
            onDeleteRun={deleteRun}
            onNavigate={navigate}
          />
        ))}
      </div>
    </div>
  )
}

function ExamCard({ exam, onCreateRun, onUpdateStatus, onDeleteExam, onDeleteRun, onNavigate }) {
  const runningCount = (exam.runs || []).filter(r => r.status === 'running').length
  const totalStudents = (exam.runs || []).reduce((sum, r) => sum + (r.registered_students || []).length, 0)

  return (
    <div className="bg-zinc-900 border border-zinc-800 rounded-xl overflow-hidden">
      {/* Exam Header */}
      <div className="p-5 border-b border-zinc-800/50">
        <div className="flex items-start justify-between">
          <div>
            <h3 className="text-lg font-semibold text-white">{exam.name}</h3>
            <div className="flex items-center gap-4 mt-1.5">
              <span className="text-xs text-zinc-500 font-mono">{exam.id.slice(0, 8)}</span>
              <span className="text-xs text-zinc-600">{(exam.runs || []).length} run{(exam.runs || []).length !== 1 ? 's' : ''}</span>
              <span className="text-xs text-zinc-600">{totalStudents} student{totalStudents !== 1 ? 's' : ''}</span>
              {runningCount > 0 && (
                <span className="flex items-center gap-1 text-xs text-emerald-400">
                  <div className="w-1.5 h-1.5 rounded-full bg-emerald-500 animate-pulse" />
                  {runningCount} active
                </span>
              )}
            </div>
          </div>
          <div className="flex items-center gap-2">
            {runningCount === 0 && (
              <button
                onClick={() => onNavigate(`/edit-exam/${exam.id}`)}
                className="px-3.5 py-1.5 bg-zinc-800 hover:bg-zinc-700 text-zinc-400 text-xs font-medium rounded-lg transition-colors border border-zinc-700"
              >
                Edit
              </button>
            )}
            <button
              onClick={onCreateRun}
              className="px-3.5 py-1.5 bg-zinc-800 hover:bg-zinc-700 text-zinc-300 text-xs font-medium rounded-lg transition-colors border border-zinc-700"
            >
              + New Run
            </button>
            {runningCount === 0 && (
              <button
                onClick={onDeleteExam}
                className="px-3.5 py-1.5 bg-zinc-800 hover:bg-red-900/80 text-zinc-500 hover:text-red-300 text-xs font-medium rounded-lg transition-colors border border-zinc-700 hover:border-red-800"
              >
                Delete
              </button>
            )}
          </div>
        </div>
      </div>

      {/* Runs */}
      {exam.runs && exam.runs.length > 0 ? (
        <div className="divide-y divide-zinc-800/50">
          {exam.runs.map((run) => (
            <RunRow key={run.id} run={run} onUpdateStatus={onUpdateStatus} onDeleteRun={onDeleteRun} onNavigate={onNavigate} />
          ))}
        </div>
      ) : (
        <div className="px-5 py-4">
          <p className="text-xs text-zinc-600">No runs yet — create one to start an exam session</p>
        </div>
      )}
    </div>
  )
}

function RunRow({ run, onUpdateStatus, onDeleteRun, onNavigate }) {
  const studentCount = (run.registered_students || []).length

  return (
    <div className="px-5 py-3.5 flex items-center justify-between hover:bg-zinc-800/30 transition-colors">
      <div className="flex items-center gap-4">
        <StatusBadge status={run.status} />
        <div>
          <span className="font-mono text-sm text-zinc-300">{run.id.slice(0, 8)}</span>
          <span className="text-xs text-zinc-600 ml-3">{studentCount} student{studentCount !== 1 ? 's' : ''}</span>
        </div>
      </div>
      <div className="flex items-center gap-2">
        {run.status === 'pending' && (
          <button
            onClick={() => onUpdateStatus(run.id, 'running')}
            className="px-3.5 py-1.5 bg-emerald-600 hover:bg-emerald-500 text-white text-xs font-medium rounded-lg transition-colors shadow-sm"
          >
            Start Exam
          </button>
        )}
        {run.status === 'running' && (
          <>
            <button
              onClick={() => onNavigate(`/monitor/${run.id}`)}
              className="px-3.5 py-1.5 bg-indigo-600 hover:bg-indigo-500 text-white text-xs font-medium rounded-lg transition-colors shadow-sm"
            >
              Live Monitor
            </button>
            <button
              onClick={() => onUpdateStatus(run.id, 'finished')}
              className="px-3.5 py-1.5 bg-zinc-800 hover:bg-red-900/80 text-zinc-400 hover:text-red-300 text-xs font-medium rounded-lg transition-colors border border-zinc-700 hover:border-red-800"
            >
              End Exam
            </button>
          </>
        )}
        {run.status === 'finished' && (
          <>
            <button
              onClick={() => onNavigate(`/work?run=${run.id}`)}
              className="px-3.5 py-1.5 bg-violet-600 hover:bg-violet-500 text-white text-xs font-medium rounded-lg transition-colors shadow-sm"
            >
              View Work
            </button>
            <button
              onClick={() => onNavigate(`/monitor/${run.id}`)}
              className="px-3.5 py-1.5 bg-zinc-800 hover:bg-zinc-700 text-zinc-300 text-xs font-medium rounded-lg transition-colors border border-zinc-700"
            >
              Review
            </button>
          </>
        )}
        <button
          onClick={() => onDeleteRun(run.id)}
          className="px-2 py-1.5 text-zinc-700 hover:text-red-400 transition-colors"
          title="Delete run"
        >
          <svg className="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={1.5} d="M19 7l-.867 12.142A2 2 0 0116.138 21H7.862a2 2 0 01-1.995-1.858L5 7m5 4v6m4-6v6m1-10V4a1 1 0 00-1-1h-4a1 1 0 00-1 1v3M4 7h16" />
          </svg>
        </button>
      </div>
    </div>
  )
}

function StatusBadge({ status }) {
  const config = {
    pending: { bg: 'bg-amber-500/10', text: 'text-amber-400', border: 'border-amber-500/30', dot: 'bg-amber-500' },
    running: { bg: 'bg-emerald-500/10', text: 'text-emerald-400', border: 'border-emerald-500/30', dot: 'bg-emerald-500' },
    finished: { bg: 'bg-zinc-500/10', text: 'text-zinc-400', border: 'border-zinc-500/30', dot: 'bg-zinc-500' },
  }
  const c = config[status] || config.pending

  return (
    <span className={`inline-flex items-center gap-1.5 px-2.5 py-1 text-xs font-medium rounded-full border ${c.bg} ${c.text} ${c.border}`}>
      <div className={`w-1.5 h-1.5 rounded-full ${c.dot} ${status === 'running' ? 'animate-pulse' : ''}`} />
      {status.charAt(0).toUpperCase() + status.slice(1)}
    </span>
  )
}
