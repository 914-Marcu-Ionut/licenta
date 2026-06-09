import { useState, useEffect, useRef } from 'react'
import { useParams, useNavigate } from 'react-router-dom'
import { api } from '../api'

export default function EditExam() {
  const { examId } = useParams()
  const navigate = useNavigate()
  const [examName, setExamName] = useState('')
  const [duration, setDuration] = useState(90)
  const [existingFiles, setExistingFiles] = useState([])
  const [removedFiles, setRemovedFiles] = useState([])
  const [newFiles, setNewFiles] = useState([])
  const [saving, setSaving] = useState(false)
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState('')
  const fileInputRef = useRef(null)

  useEffect(() => { loadExam() }, [examId])

  async function loadExam() {
    const r = await api('GET', `/exam?id=${examId}`)
    if (r.ok) {
      setExamName(r.data.name || '')
      try {
        const settings = JSON.parse(r.data.settings || '{}')
        setDuration(settings.exam_duration || 90)
        setExistingFiles(settings.predefined_files || [])
      } catch {}
    }
    setLoading(false)
  }

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
          if (f) setNewFiles(prev => [...prev, f])
        }
      }
    }
  }

  function traverseEntry(entry, path) {
    if (entry.isFile) {
      entry.file(f => {
        const fullPath = path ? `${path}/${f.name}` : f.name
        Object.defineProperty(f, 'relativePath', { value: fullPath })
        setNewFiles(prev => [...prev, f])
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
    setNewFiles(prev => [...prev, ...selected])
  }

  function removeExisting(index) {
    const file = existingFiles[index]
    setRemovedFiles(prev => [...prev, file])
    setExistingFiles(prev => prev.filter((_, i) => i !== index))
  }

  function removeNew(index) {
    setNewFiles(prev => prev.filter((_, i) => i !== index))
  }

  async function handleSubmit(e) {
    e.preventDefault()
    setError('')

    if (!examName.trim()) {
      setError('Exam name is required')
      return
    }

    setSaving(true)

    try {
      const allFiles = [
        ...existingFiles,
        ...newFiles.map(f => f.relativePath || f.webkitRelativePath || f.name),
      ]

      const settings = JSON.stringify({
        exam_duration: duration,
        predefined_files: allFiles,
      })

      const r = await api('PUT', '/exam', {
        id: examId,
        name: examName.trim(),
        settings,
      })

      if (!r.ok) {
        setError(r.data?.error || 'Failed to update exam')
        setSaving(false)
        return
      }

      if (removedFiles.length > 0) {
        await api('POST', '/exam/files/delete', {
          exam_id: examId,
          files: removedFiles,
        })
      }

      if (newFiles.length > 0) {
        const formData = new FormData()
        formData.append('exam_id', examId)
        for (const f of newFiles) {
          const path = f.relativePath || f.webkitRelativePath || f.name
          formData.append('files', f, path)
        }

        const uploadRes = await fetch('/api/exam/files', {
          method: 'POST',
          body: formData,
        })

        if (!uploadRes.ok) {
          setError('Settings saved but file upload failed')
          setSaving(false)
          return
        }
      }

      navigate('/')
    } catch (err) {
      setError('Unexpected error: ' + err.message)
    } finally {
      setSaving(false)
    }
  }

  if (loading) {
    return <div className="text-zinc-500 text-sm">Loading...</div>
  }

  return (
    <div className="max-w-2xl mx-auto space-y-8">
      <div>
        <button
          onClick={() => navigate('/')}
          className="text-sm text-zinc-500 hover:text-zinc-300 transition-colors mb-4"
        >
          ← Back to Dashboard
        </button>
        <h2 className="text-2xl font-bold text-white">Edit Exam</h2>
        <p className="text-sm text-zinc-500 mt-1">Update exam settings and materials</p>
      </div>

      <form onSubmit={handleSubmit} className="space-y-6">
        {/* Exam Name */}
        <div className="bg-zinc-900 border border-zinc-800 rounded-xl p-6 space-y-4">
          <div>
            <label className="block text-sm font-medium text-zinc-300 mb-2">Exam Name</label>
            <input
              type="text"
              value={examName}
              onChange={(e) => setExamName(e.target.value)}
              className="w-full px-4 py-3 bg-zinc-950 border border-zinc-700 rounded-lg text-white text-sm focus:outline-none focus:border-indigo-500 focus:ring-1 focus:ring-indigo-500 transition-colors"
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

        {/* Existing Files */}
        <div className="bg-zinc-900 border border-zinc-800 rounded-xl p-6 space-y-4">
          <div>
            <label className="block text-sm font-medium text-zinc-300 mb-1">Exam Materials</label>
            <p className="text-xs text-zinc-500 mb-4">Manage uploaded files or add new ones</p>
          </div>

          {existingFiles.length > 0 && (
            <div className="space-y-1 mb-4">
              <span className="text-xs font-medium text-zinc-400 mb-2 block">
                Current files ({existingFiles.length})
              </span>
              {existingFiles.map((f, i) => (
                <div key={i} className="flex items-center justify-between bg-zinc-950 rounded-lg px-3 py-2 group">
                  <span className="text-xs text-zinc-300 font-mono truncate">{f}</span>
                  <button
                    type="button"
                    onClick={() => removeExisting(i)}
                    className="text-zinc-700 hover:text-red-400 transition-colors opacity-0 group-hover:opacity-100"
                  >
                    <svg className="w-3.5 h-3.5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                      <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M6 18L18 6M6 6l12 12" />
                    </svg>
                  </button>
                </div>
              ))}
            </div>
          )}

          {/* Drop Zone */}
          <div
            onDragOver={(e) => { e.preventDefault(); e.stopPropagation() }}
            onDrop={handleFileDrop}
            onClick={() => fileInputRef.current?.click()}
            className="border-2 border-dashed border-zinc-700 hover:border-indigo-500/50 rounded-xl p-6 text-center cursor-pointer transition-colors group"
          >
            <p className="text-sm text-zinc-500 group-hover:text-zinc-400">Drop new files here or click to browse</p>
          </div>

          <input
            ref={fileInputRef}
            type="file"
            multiple
            onChange={handleFileInput}
            className="hidden"
          />

          {newFiles.length > 0 && (
            <div className="space-y-1">
              <div className="flex items-center justify-between mb-2">
                <span className="text-xs font-medium text-emerald-400">
                  + {newFiles.length} new file{newFiles.length !== 1 ? 's' : ''}
                </span>
                <button
                  type="button"
                  onClick={() => setNewFiles([])}
                  className="text-xs text-zinc-600 hover:text-red-400 transition-colors"
                >
                  Remove all new
                </button>
              </div>
              <div className="max-h-40 overflow-y-auto space-y-1">
                {newFiles.map((f, i) => (
                  <div key={i} className="flex items-center justify-between bg-emerald-950/20 border border-emerald-900/30 rounded-lg px-3 py-2 group">
                    <span className="text-xs text-emerald-300 font-mono truncate">
                      {f.relativePath || f.webkitRelativePath || f.name}
                    </span>
                    <button
                      type="button"
                      onClick={() => removeNew(i)}
                      className="text-zinc-700 hover:text-red-400 transition-colors opacity-0 group-hover:opacity-100"
                    >
                      <svg className="w-3.5 h-3.5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                        <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M6 18L18 6M6 6l12 12" />
                      </svg>
                    </button>
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
            disabled={saving}
            className="px-6 py-3 bg-indigo-600 hover:bg-indigo-500 disabled:bg-indigo-800 disabled:opacity-50 text-white text-sm font-medium rounded-xl transition-all shadow-lg shadow-indigo-900/20"
          >
            {saving ? 'Saving...' : 'Save Changes'}
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
