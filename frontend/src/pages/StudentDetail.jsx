import { useState, useEffect, useRef } from 'react'
import { useParams, useNavigate, useSearchParams } from 'react-router-dom'
import { api, createExamSocket } from '../api'

export default function StudentDetail({ teacher, password }) {
  const { studentId } = useParams()
  const [searchParams] = useSearchParams()
  const runId = searchParams.get('run') || ''
  const navigate = useNavigate()
  const [student, setStudent] = useState(null)
  const [events, setEvents] = useState([])
  const [flags, setFlags] = useState([])
  const [connected, setConnected] = useState(false)
  const [wsStatus, setWsStatus] = useState('disconnected')
  const [tcpConnections, setTcpConnections] = useState([])
  const [vmInfo, setVmInfo] = useState(null)
  const [loading, setLoading] = useState(true)
  const wsRef = useRef(null)

  useEffect(() => {
    loadInitialData()
  }, [studentId])

  useEffect(() => {
    if (!runId || !teacher) return

    const ws = createExamSocket(runId, teacher.name, password, {
      onStatus: (s) => setWsStatus(s),
      onMessage: (msg) => {
        if (msg.message === 'student_event' && msg.data?.student_id === studentId) {
          const d = msg.data

          if (d.type === 'tcp_snapshot') {
            try {
              const det = JSON.parse(d.detail || '{}')
              setTcpConnections(det.connections || [])
            } catch {}
            return
          }

          if (d.type === 'vm_detection') {
            try { setVmInfo(JSON.parse(d.detail || '{}')) } catch {}
          }

          if (d.type === 'heartbeat') return

          setEvents((prev) => [{
            type: d.type,
            detail: d.detail,
            created_at: new Date().toISOString(),
          }, ...prev].slice(0, 300))

          setConnected(true)
        }

        if (msg.message === 'student_flag' && msg.data?.student_id === studentId) {
          const d = msg.data
          setFlags((prev) => [...prev, {
            type: d.flag,
            detail: d.detail,
            severity: d.severity || 'medium',
            timestamp: new Date().toISOString(),
          }])
        }

        if (msg.message === 'student_status' && msg.data?.student_id === studentId) {
          setConnected(msg.data.status === 'online')
        }
      },
    })
    wsRef.current = ws
    return () => ws.close()
  }, [runId, teacher?.name, password, studentId])

  async function loadInitialData() {
    setLoading(true)

    if (runId) {
      const r = await api('GET', `/exam/run?id=${runId}`)
      if (r.ok) {
        const s = (r.data.registered_students || []).find(s => s.id === studentId)
        if (s) {
          setStudent(s)
          try {
            const stats = JSON.parse(s.stats || '{}')
            setFlags(stats.flags || [])
            setConnected(stats.connected ?? false)
          } catch {}
        }
      }
    }

    const eventsRes = await api('GET', `/student/events?id=${studentId}`)
    if (eventsRes.ok) {
      const allEvents = eventsRes.data || []

      const tcpEv = allEvents.find(e => e.type === 'tcp_snapshot')
      if (tcpEv) {
        try {
          const det = JSON.parse(tcpEv.detail || '{}')
          setTcpConnections(det.connections || [])
        } catch {}
      }

      const vmEv = allEvents.find(e => e.type === 'vm_detection')
      if (vmEv) {
        try { setVmInfo(JSON.parse(vmEv.detail || '{}')) } catch {}
      }

      setEvents(allEvents.filter(e => e.type !== 'tcp_snapshot' && e.type !== 'vm_detection'))
    }

    setLoading(false)
  }

  if (loading) {
    return <div className="text-zinc-500 text-sm">Loading...</div>
  }

  return (
    <div className="space-y-6">
      <button
        onClick={() => navigate(-1)}
        className="text-sm text-zinc-500 hover:text-zinc-300 transition-colors"
      >
        ← Back
      </button>

      {/* Header */}
      <div className="bg-zinc-900 border border-zinc-800 rounded-xl p-6">
        <div className="flex items-center justify-between">
          <div>
            <h2 className="text-xl font-semibold text-white">{student?.name || studentId}</h2>
            <p className="text-sm text-zinc-500 font-mono mt-1">{studentId}</p>
          </div>
          <div className="flex items-center gap-3">
            <WsIndicator status={wsStatus} />
            <span className={`flex items-center gap-1.5 text-xs px-2.5 py-1 rounded-full ${connected ? 'bg-emerald-950 text-emerald-400 border border-emerald-800' : 'bg-amber-950 text-amber-400 border border-amber-800'}`}>
              <div className={`w-2 h-2 rounded-full ${connected ? 'bg-emerald-500' : 'bg-amber-500 animate-pulse'}`} />
              {connected ? 'Online' : 'Offline'}
            </span>
            {vmInfo && (
              <span className={`text-xs px-2 py-1 rounded ${vmInfo.is_vm ? 'bg-red-950 text-red-400 border border-red-900' : 'bg-emerald-950 text-emerald-400 border border-emerald-900'}`}>
                {vmInfo.is_vm ? `VM: ${vmInfo.reason}` : 'No VM'}
              </span>
            )}
          </div>
        </div>
      </div>

      {/* Flags */}
      <Section title={`Flags (${flags.length})`}>
        {flags.length === 0 ? (
          <p className="text-zinc-600 text-sm">No flags recorded.</p>
        ) : (
          <div className="space-y-2">
            {flags.map((f, i) => (
              <div key={i} className={`flex items-start gap-3 px-3 py-2 rounded-lg ${severityBg(f.severity)}`}>
                <span className={`text-xs font-semibold shrink-0 mt-0.5 ${severityText(f.severity)}`}>
                  {f.severity?.toUpperCase() || 'LOW'}
                </span>
                <div className="min-w-0">
                  <span className="text-sm text-white font-medium">{f.type}</span>
                  <p className="text-xs text-zinc-400 truncate">{f.detail}</p>
                </div>
                <span className="text-[10px] text-zinc-600 shrink-0 ml-auto">
                  {f.timestamp ? new Date(f.timestamp).toLocaleTimeString() : ''}
                </span>
              </div>
            ))}
          </div>
        )}
      </Section>

      {/* Live Events */}
      <Section title={`Events (${events.length})`}>
        {events.length === 0 ? (
          <p className="text-zinc-600 text-sm">No events recorded.</p>
        ) : (
          <div className="max-h-80 overflow-y-auto divide-y divide-zinc-800/50">
            {events.slice(0, 100).map((e, i) => (
              <div key={i} className="flex items-baseline gap-3 py-1.5 text-xs">
                <span className="text-zinc-600 font-mono shrink-0">
                  {new Date(e.created_at).toLocaleTimeString()}
                </span>
                <span className="text-indigo-400 font-medium">{e.type}</span>
                <span className="text-zinc-500 truncate">{summarizeDetail(e.type, e.detail)}</span>
              </div>
            ))}
          </div>
        )}
      </Section>

      {/* TCP Connections */}
      <TcpSection connections={tcpConnections} />
    </div>
  )
}

function TcpSection({ connections }) {
  const [geoCache, setGeoCache] = useState({})

  useEffect(() => {
    if (connections.length === 0) return
    const unknownIps = [...new Set(connections.map(c => c.remote_ip))]
      .filter(ip => !geoCache[ip] && !ip.startsWith('127.') && !ip.startsWith('0.') && ip !== '::1')

    if (unknownIps.length === 0) return

    fetch('http://ip-api.com/batch?fields=query,country,city,org', {
      method: 'POST',
      body: JSON.stringify(unknownIps.slice(0, 100)),
    })
      .then(r => r.json())
      .then(results => {
        const newCache = { ...geoCache }
        for (const r of results) {
          if (r.query) {
            newCache[r.query] = { country: r.country || '', city: r.city || '', org: r.org || '' }
          }
        }
        setGeoCache(newCache)
      })
      .catch(() => {})
  }, [connections])

  return (
    <Section title={`TCP Connections (${connections.length})`}>
      {connections.length === 0 ? (
        <p className="text-zinc-600 text-sm">No active connections recorded.</p>
      ) : (
        <div className="overflow-x-auto">
          <table className="w-full text-sm">
            <thead>
              <tr className="text-left text-xs text-zinc-500 border-b border-zinc-800">
                <th className="pb-2 pr-4">Process</th>
                <th className="pb-2 pr-4">Remote IP</th>
                <th className="pb-2 pr-4">Port</th>
                <th className="pb-2 pr-4">Location</th>
                <th className="pb-2">Organization</th>
              </tr>
            </thead>
            <tbody className="divide-y divide-zinc-800/50">
              {connections.map((c, i) => {
                const geo = geoCache[c.remote_ip]
                return (
                  <tr key={i} className="text-zinc-300">
                    <td className="py-1.5 pr-4 font-mono text-xs">{c.process || '—'}</td>
                    <td className="py-1.5 pr-4 font-mono text-xs">{c.remote_ip}</td>
                    <td className="py-1.5 pr-4 text-zinc-500">{c.remote_port}</td>
                    <td className="py-1.5 pr-4 text-xs text-zinc-400">
                      {geo ? `${geo.city}${geo.city && geo.country ? ', ' : ''}${geo.country}` : '—'}
                    </td>
                    <td className="py-1.5 text-xs text-zinc-500 truncate max-w-48">
                      {geo?.org || '—'}
                    </td>
                  </tr>
                )
              })}
            </tbody>
          </table>
        </div>
      )}
    </Section>
  )
}

function WsIndicator({ status }) {
  const color = status === 'connected' ? 'bg-emerald-500' : 'bg-red-500'
  return (
    <div className="flex items-center gap-1.5">
      <div className={`w-1.5 h-1.5 rounded-full ${color} ${status === 'connected' ? 'animate-pulse' : ''}`} />
      <span className="text-[10px] text-zinc-500 uppercase tracking-wider">
        {status === 'connected' ? 'Live' : 'Offline'}
      </span>
    </div>
  )
}

function Section({ title, children }) {
  return (
    <div className="bg-zinc-900 border border-zinc-800 rounded-xl p-5">
      <h3 className="text-sm font-semibold text-zinc-300 mb-3">{title}</h3>
      {children}
    </div>
  )
}

function severityBg(severity) {
  if (severity === 'high') return 'bg-red-950/30 border border-red-900/50'
  if (severity === 'medium') return 'bg-amber-950/30 border border-amber-900/50'
  return 'bg-zinc-800/30 border border-zinc-700/50'
}

function severityText(severity) {
  if (severity === 'high') return 'text-red-400'
  if (severity === 'medium') return 'text-amber-400'
  return 'text-zinc-400'
}

function summarizeDetail(type, detail) {
  try {
    const d = JSON.parse(detail || '{}')
    if (type === 'clipboard_text' && d.text) return `"${d.text.substring(0, 80)}"`
    if (type === 'clipboard_file' && d.files?.length) return d.files.join(', ')
    if (type?.startsWith('file_') && d.path) return d.path
    if (type === 'usb_connected' || type === 'usb_disconnected') return d.drive
    if (type === 'vm_detection') return d.is_vm ? d.reason : 'clean'
  } catch {}
  return ''
}
