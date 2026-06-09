import { useState, useRef } from 'react'
import { useNavigate } from 'react-router-dom'
import { api } from '../api'

export default function CreateExam({ teacher }) {
  const navigate = useNavigate()
  const [examName, setExamName] = useState('')
  const [duration, setDuration] = useState(90)
  const [files, setFiles] = useState([])
  const [uploading, setUploading] = useState(false)
  const [error, setError] = useState('')
  const fileInputRef = useRef(null)

  function handleFileDrop(e) {
    e.preventDefault()
    e.stopPropagation()
    const items = e.dataTransfer?.items
    if (items) {
      for (let i = 0; i < items.length; i++) {
        const entry = items[i].webkitGetAsEntry?.()
        if (entry) {
          traverseEntry(entry, '')
        } else if (items[i].kind === 'file') {
          const f = items[i].getAsFile()
          if (f) setFiles(prev => [...prev, f])
        }
      }
    }
  }

  function traverseEntry(entry, path) {
    if (entry.isFile) {
      entry.file(f => {
        const fullPath = path ? `${path}/${f.name}` : f.name
        Object.defineProperty(f, 'relativePath', { value: fullPath })
        setFiles(prev => [...prev, f])
      })
    } else if (entry.isDirectory) {
      const reader = entry.createReader()
      reader.readEntries(entries => {
        for (const e of entries) {
          traverseEntry(e, path ? `${path}/${entry.name}` : entry.name)
        }
      })
    }
  }

  function handleFileInput(e) {
    const selected = Array.from(e.target.files || [])
    setFiles(prev => [...prev, ...selected])
  }

  function removeFile(index) {
    setFiles(prev => prev.filter((_, i) => i !== index))
  }

  async function handleSubmit(e) {
    e.preventDefault()
    setError('')

    if (!examName.trim()) {
      setError('Exam name is required')
      return
    }

    setUploading(true)

    try {
      const settings = JSON.stringify({
        exam_duration: duration,
        predefined_files: files.map(f => f.relativePath || f.webkitRelativePath || f.name),
      })

      const examRes = await api('POST', '/exam', {
        created_by: teacher.id,
        name: examName.trim(),
        settings,
      })

      if (!examRes.ok) {
        setError(examRes.data?.error || 'Failed to create exam')
        setUploading(false)
        return
      }

      const examId = examRes.data.id

      if (files.length > 0) {
        const formData = new FormData()
        formData.append('exam_id', examId)
        for (const f of files) {
          const path = f.relativePath || f.webkitRelativePath || f.name
          formData.append('files', f, path)
        }

        const uploadRes = await fetch('/api/exam/files', {
          method: 'POST',
          body: formData,
        })

        if (!uploadRes.ok) {
          setError('Exam created but file upload failed')
          setUploading(false)
          return
        }
      }

      navigate('/')
    } catch (err) {
      setError('Unexpected error: ' + err.message)
    } finally {
      setUploading(false)
    }
  }

  const totalSize = files.reduce((sum, f) => sum + (f.size || 0), 0)

  return (
    <div className="max-w-2xl mx-auto space-y-8">
      <div>
        <button
          onClick={() => navigate('/')}
          className="text-sm text-zinc-500 hover:text-zinc-300 transition-colors mb-4"
        >
          ← Back to Dashboard
        </button>
        <h2 className="text-2xl font-bold text-white">Create New Exam</h2>
        <p className="text-sm text-zinc-500 mt-1">Configure the exam settings and upload materials for students</p>
      </div>

      <form onSubmit={handleSubmit} className="space-y-6">
        {/* Exam Name */}
        <div className="bg-zinc-900 border border-zinc-800 rounded-xl p-6 space-y-4">
          <div>
            <label className="block text-sm font-medium text-zinc-300 mb-2">Exam Name</label>
            <input
              type="text"
              placeholder="e.g. Data Structures — Final Exam 2025"
              value={examName}
              onChange={(e) => setExamName(e.target.value)}
              className="w-full px-4 py-3 bg-zinc-950 border border-zinc-700 rounded-lg text-white text-sm focus:outline-none focus:border-indigo-500 focus:ring-1 focus:ring-indigo-500 transition-colors placeholder-zinc-600"
              required
            />
          </div>

          <div>
            <label className="block text-sm font-medium text-zinc-300 mb-2">Duration (minutes)</label>
            <div className="flex items-center gap-4">
              <input
                type="range"
                min="15"
                max="300"
                step="5"
                value={duration}
                onChange={(e) => setDuration(Number(e.target.value))}
                className="flex-1 accent-indigo-500"
              />
              <div className="flex items-center gap-2">
                <input
                  type="number"
                  min="15"
                  max="300"
                  value={duration}
                  onChange={(e) => setDuration(Number(e.target.value))}
                  className="w-20 px-3 py-2 bg-zinc-950 border border-zinc-700 rounded-lg text-white text-sm text-center focus:outline-none focus:border-indigo-500"
                />
                <span className="text-sm text-zinc-500">min</span>
              </div>
            </div>
            <p className="text-xs text-zinc-600 mt-1">
              {Math.floor(duration / 60)}h {duration % 60}m
            </p>
          </div>
        </div>

        {/* File Upload */}
        <div className="bg-zinc-900 border border-zinc-800 rounded-xl p-6 space-y-4">
          <div>
            <label className="block text-sm font-medium text-zinc-300 mb-1">Exam Materials</label>
            <p className="text-xs text-zinc-500 mb-4">Upload subjects, documentation, starter code, or any files students will need</p>
          </div>

          {/* Drop Zone */}
          <div
            onDragOver={(e) => { e.preventDefault(); e.stopPropagation() }}
            onDrop={handleFileDrop}
            onClick={() => fileInputRef.current?.click()}
            className="border-2 border-dashed border-zinc-700 hover:border-indigo-500/50 rounded-xl p-8 text-center cursor-pointer transition-colors group"
          >
            <div className="text-zinc-500 group-hover:text-zinc-400 transition-colors">
              <svg className="w-10 h-10 mx-auto mb-3 opacity-50" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={1.5} d="M7 16a4 4 0 01-.88-7.903A5 5 0 1115.9 6L16 6a5 5 0 011 9.9M15 13l-3-3m0 0l-3 3m3-3v12" />
              </svg>
              <p className="text-sm font-medium">Drop files or folders here</p>
              <p className="text-xs text-zinc-600 mt-1">or click to browse</p>
            </div>
          </div>

          <input
            ref={fileInputRef}
            type="file"
            multiple
            onChange={handleFileInput}
            className="hidden"
          />

          {/* File List */}
          {files.length > 0 && (
            <div className="space-y-1">
              <div className="flex items-center justify-between mb-2">
                <span className="text-xs font-medium text-zinc-400">
                  {files.length} file{files.length !== 1 ? 's' : ''} ({formatSize(totalSize)})
                </span>
                <button
                  type="button"
                  onClick={() => setFiles([])}
                  className="text-xs text-zinc-600 hover:text-red-400 transition-colors"
                >
                  Remove all
                </button>
              </div>
              <div className="max-h-48 overflow-y-auto space-y-1 pr-1">
                {files.map((f, i) => (
                  <div key={i} className="flex items-center justify-between bg-zinc-950 rounded-lg px-3 py-2 group">
                    <div className="flex items-center gap-2 min-w-0">
                      <FileIcon name={f.name} />
                      <span className="text-xs text-zinc-300 truncate">
                        {f.relativePath || f.webkitRelativePath || f.name}
                      </span>
                    </div>
                    <div className="flex items-center gap-2 shrink-0">
                      <span className="text-[10px] text-zinc-600">{formatSize(f.size)}</span>
                      <button
                        type="button"
                        onClick={() => removeFile(i)}
                        className="text-zinc-700 hover:text-red-400 transition-colors opacity-0 group-hover:opacity-100"
                      >
                        <svg className="w-3.5 h-3.5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                          <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M6 18L18 6M6 6l12 12" />
                        </svg>
                      </button>
                    </div>
                  </div>
                ))}
              </div>
            </div>
          )}
        </div>

        {/* Error */}
        {error && (
          <div className="bg-red-950/50 border border-red-900 rounded-lg px-4 py-3">
            <p className="text-sm text-red-400">{error}</p>
          </div>
        )}

        {/* Submit */}
        <div className="flex items-center gap-4">
          <button
            type="submit"
            disabled={uploading}
            className="px-6 py-3 bg-indigo-600 hover:bg-indigo-500 disabled:bg-indigo-800 disabled:opacity-50 text-white text-sm font-medium rounded-xl transition-all shadow-lg shadow-indigo-900/20"
          >
            {uploading ? 'Creating...' : 'Create Exam'}
          </button>
          <button
            type="button"
            onClick={() => navigate('/')}
            className="px-6 py-3 bg-zinc-800 hover:bg-zinc-700 text-zinc-300 text-sm font-medium rounded-xl transition-colors"
          >
            Cancel
          </button>
        </div>
      </form>
    </div>
  )
}

function FileIcon({ name }) {
  const ext = name?.split('.').pop()?.toLowerCase() || ''
  let color = 'text-zinc-500'
  if (['pdf'].includes(ext)) color = 'text-red-400'
  else if (['doc', 'docx', 'txt', 'md'].includes(ext)) color = 'text-blue-400'
  else if (['zip', 'rar', '7z', 'tar', 'gz'].includes(ext)) color = 'text-amber-400'
  else if (['py', 'java', 'c', 'cpp', 'h', 'js', 'ts', 'go', 'rs'].includes(ext)) color = 'text-emerald-400'
  else if (['png', 'jpg', 'jpeg', 'gif', 'svg'].includes(ext)) color = 'text-purple-400'

  return (
    <svg className={`w-4 h-4 shrink-0 ${color}`} fill="none" stroke="currentColor" viewBox="0 0 24 24">
      <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={1.5} d="M9 12h6m-6 4h6m2 5H7a2 2 0 01-2-2V5a2 2 0 012-2h5.586a1 1 0 01.707.293l5.414 5.414a1 1 0 01.293.707V19a2 2 0 01-2 2z" />
    </svg>
  )
}

function formatSize(bytes) {
  if (!bytes) return '0 B'
  if (bytes < 1024) return bytes + ' B'
  if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB'
  return (bytes / (1024 * 1024)).toFixed(1) + ' MB'
}
