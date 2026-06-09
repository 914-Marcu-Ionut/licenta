import { useState, useEffect } from 'react'
import { useSearchParams, useNavigate } from 'react-router-dom'
import { api } from '../api'

export default function StudentWork({ teacher }) {
  const [searchParams] = useSearchParams()
  const runId = searchParams.get('run') || ''
  const navigate = useNavigate()

  const [students, setStudents] = useState([])
  const [selectedStudent, setSelectedStudent] = useState(null)
  const [files, setFiles] = useState([])
  const [selectedFile, setSelectedFile] = useState(null)
  const [fileContent, setFileContent] = useState('')
  const [loading, setLoading] = useState(true)
  const [fileLoading, setFileLoading] = useState(false)

  useEffect(() => {
    if (!runId) return
    api('GET', `/student/work?run_id=${runId}`).then(({ data }) => {
      setStudents(data || [])
      setLoading(false)
    })
  }, [runId])

  const selectStudent = async (student) => {
    setSelectedStudent(student)
    setSelectedFile(null)
    setFileContent('')
    if (!student.uploaded) { setFiles([]); return }
    const { data } = await api('GET', `/student/work/files?run_id=${runId}&student_id=${student.id}`)
    setFiles((data || []).filter(f => !f.dir))
  }

  const selectFile = async (file) => {
    setSelectedFile(file)
    setFileLoading(true)
    const res = await fetch(`/api/student/work/file?run_id=${runId}&student_id=${selectedStudent.id}&path=${encodeURIComponent(file.path)}`)
    const text = await res.text()
    setFileContent(text)
    setFileLoading(false)
  }

  const buildTree = (files) => {
    const tree = {}
    files.forEach(f => {
      const parts = f.path.replace(/\\/g, '/').split('/')
      let node = tree
      parts.forEach((p, i) => {
        if (i === parts.length - 1) {
          node[p] = f
        } else {
          if (!node[p] || typeof node[p] !== 'object' || node[p].path) node[p] = {}
          node = node[p]
        }
      })
    })
    return tree
  }

  const extColor = (name) => {
    const ext = name.split('.').pop().toLowerCase()
    const map = {
      js: 'text-yellow-400', jsx: 'text-yellow-400', ts: 'text-blue-400', tsx: 'text-blue-400',
      py: 'text-green-400', go: 'text-cyan-400', java: 'text-orange-400', c: 'text-blue-300',
      cpp: 'text-blue-300', h: 'text-purple-400', cs: 'text-green-300', html: 'text-orange-300',
      css: 'text-pink-400', json: 'text-yellow-300', md: 'text-gray-300', txt: 'text-gray-400',
      sql: 'text-red-300', xml: 'text-orange-300', yml: 'text-pink-300', yaml: 'text-pink-300',
    }
    return map[ext] || 'text-gray-400'
  }

  const formatSize = (bytes) => {
    if (bytes < 1024) return bytes + ' B'
    if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB'
    return (bytes / (1024 * 1024)).toFixed(1) + ' MB'
  }

  const TreeNode = ({ name, node, depth = 0 }) => {
    const [open, setOpen] = useState(depth < 2)
    if (node.path !== undefined) {
      const isSelected = selectedFile?.path === node.path
      return (
        <button
          onClick={() => selectFile(node)}
          className={`w-full text-left px-2 py-0.5 text-sm flex items-center gap-1 rounded hover:bg-zinc-700 ${isSelected ? 'bg-zinc-700 text-white' : 'text-zinc-300'}`}
          style={{ paddingLeft: depth * 12 + 8 }}
        >
          <span className={extColor(name)}>●</span>
          <span className="truncate">{name}</span>
          <span className="ml-auto text-xs text-zinc-500">{formatSize(node.size)}</span>
        </button>
      )
    }
    const entries = Object.entries(node)
    const folders = entries.filter(([, v]) => v.path === undefined).sort(([a], [b]) => a.localeCompare(b))
    const fileEntries = entries.filter(([, v]) => v.path !== undefined).sort(([a], [b]) => a.localeCompare(b))
    return (
      <div>
        <button
          onClick={() => setOpen(!open)}
          className="w-full text-left px-2 py-0.5 text-sm flex items-center gap-1 text-zinc-200 hover:bg-zinc-700 rounded"
          style={{ paddingLeft: depth * 12 + 8 }}
        >
          <span className="text-xs">{open ? '▼' : '▶'}</span>
          <span className="text-amber-400">📁</span>
          <span>{name}</span>
        </button>
        {open && (
          <div>
            {folders.map(([k, v]) => <TreeNode key={k} name={k} node={v} depth={depth + 1} />)}
            {fileEntries.map(([k, v]) => <TreeNode key={k} name={k} node={v} depth={depth + 1} />)}
          </div>
        )}
      </div>
    )
  }

  const tree = buildTree(files)

  if (!runId) return <div className="p-8 text-zinc-400">No run selected.</div>

  return (
    <div className="h-screen flex flex-col bg-zinc-900 text-zinc-100">
      {/* Header */}
      <div className="flex items-center gap-4 px-6 py-3 border-b border-zinc-800 bg-zinc-900/80 backdrop-blur">
        <button onClick={() => navigate(-1)} className="text-zinc-400 hover:text-white text-sm">← Back</button>
        <h1 className="text-lg font-semibold">Student Work</h1>
        <span className="text-sm text-zinc-500">Run: {runId.slice(0, 8)}</span>
      </div>

      <div className="flex flex-1 overflow-hidden">
        {/* Students sidebar */}
        <div className="w-56 border-r border-zinc-800 overflow-y-auto bg-zinc-900">
          <div className="p-3 text-xs font-semibold text-zinc-500 uppercase tracking-wider">Students</div>
          {loading ? (
            <div className="p-4 text-sm text-zinc-500">Loading...</div>
          ) : students.length === 0 ? (
            <div className="p-4 text-sm text-zinc-500">No students</div>
          ) : (
            students.map(s => (
              <button
                key={s.id}
                onClick={() => selectStudent(s)}
                className={`w-full text-left px-3 py-2 text-sm border-b border-zinc-800/50 flex items-center gap-2 transition-colors
                  ${selectedStudent?.id === s.id ? 'bg-zinc-800 text-white' : 'text-zinc-300 hover:bg-zinc-800/50'}`}
              >
                <span className={`w-2 h-2 rounded-full ${s.uploaded ? 'bg-green-500' : 'bg-zinc-600'}`} />
                <span className="truncate flex-1">{s.name}</span>
                {s.uploaded && <span className="text-xs text-zinc-500">{formatSize(s.file_size)}</span>}
              </button>
            ))
          )}
        </div>

        {/* File tree */}
        <div className="w-64 border-r border-zinc-800 overflow-y-auto bg-zinc-900/50">
          <div className="p-3 flex items-center justify-between">
            <span className="text-xs font-semibold text-zinc-500 uppercase tracking-wider">Files</span>
            {selectedStudent?.uploaded && (
              <a
                href={`/api/student/work/download?run_id=${runId}&student_id=${selectedStudent.id}`}
                className="text-xs px-2 py-1 rounded bg-blue-600 hover:bg-blue-500 text-white transition-colors"
              >
                Download
              </a>
            )}
          </div>
          {!selectedStudent ? (
            <div className="p-4 text-sm text-zinc-500">Select a student</div>
          ) : !selectedStudent.uploaded ? (
            <div className="p-4 text-sm text-zinc-500">No work uploaded</div>
          ) : files.length === 0 ? (
            <div className="p-4 text-sm text-zinc-500">Loading...</div>
          ) : (
            Object.entries(tree).map(([k, v]) => <TreeNode key={k} name={k} node={v} depth={0} />)
          )}
        </div>

        {/* Code viewer */}
        <div className="flex-1 overflow-hidden flex flex-col">
          {selectedFile ? (
            <>
              <div className="flex items-center gap-2 px-4 py-2 border-b border-zinc-800 bg-zinc-800/50">
                <span className={`${extColor(selectedFile.path)} text-xs`}>●</span>
                <span className="text-sm text-zinc-200 font-mono">{selectedFile.path}</span>
                <span className="ml-auto text-xs text-zinc-500">{formatSize(selectedFile.size)}</span>
              </div>
              <div className="flex-1 overflow-auto bg-zinc-950">
                {fileLoading ? (
                  <div className="p-4 text-zinc-500">Loading file...</div>
                ) : (
                  <pre className="p-4 text-sm font-mono text-zinc-200 leading-relaxed">
                    <code>{fileContent.split('\n').map((line, i) => (
                      <div key={i} className="flex">
                        <span className="inline-block w-12 text-right pr-4 text-zinc-600 select-none text-xs leading-relaxed">{i + 1}</span>
                        <span className="flex-1 whitespace-pre-wrap break-all">{line}</span>
                      </div>
                    ))}</code>
                  </pre>
                )}
              </div>
            </>
          ) : (
            <div className="flex-1 flex items-center justify-center text-zinc-500">
              {selectedStudent ? 'Select a file to view' : 'Select a student to browse their work'}
            </div>
          )}
        </div>
      </div>
    </div>
  )
}