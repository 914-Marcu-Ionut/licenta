const typeColors = {
  clipboard_text: 'text-amber-400',
  clipboard_file: 'text-amber-400',
  firewall_tampered: 'text-red-400',
  firewall_error: 'text-red-400',
  firewall_ok: 'text-emerald-400',
  heartbeat: 'text-zinc-600',
  session_lock: 'text-orange-400',
  session_unlock: 'text-orange-400',
  session_logoff: 'text-orange-400',
  session_ending: 'text-orange-400',
  session_shutdown_requested: 'text-red-400',
  usb_connected: 'text-red-400',
  usb_disconnected: 'text-zinc-400',
  file_created: 'text-violet-400',
  file_deleted: 'text-violet-400',
  file_modified: 'text-violet-400',
  file_renamed_from: 'text-violet-400',
  file_renamed_to: 'text-violet-400',
  vm_detection: 'text-red-400',
  tcp_snapshot: 'text-zinc-500',
  initial_snapshot: 'text-sky-400',
  suspicious_connection: 'text-red-400',
}

export default function EventLog({ events }) {
  if (events.length === 0) {
    return (
      <div className="bg-zinc-900 border border-zinc-800 rounded-xl p-8 text-center text-zinc-600 text-sm">
        No events yet. Waiting for student activity...
      </div>
    )
  }

  return (
    <div className="bg-zinc-900 border border-zinc-800 rounded-xl overflow-hidden">
      <div className="max-h-[600px] overflow-y-auto divide-y divide-zinc-800/50">
        {events.map((event, i) => (
          <EventEntry key={i} event={event} />
        ))}
      </div>
    </div>
  )
}

function EventEntry({ event }) {
  const time = event.time.toLocaleTimeString()
  const typeClass = event.isFlag
    ? 'text-red-400 font-semibold'
    : (typeColors[event.type] || 'text-zinc-400')

  let detailText = ''
  try {
    const det = JSON.parse(event.detail)
    if (event.type === 'clipboard_text' && det.text) {
      detailText = `"${det.text.substring(0, 120)}"`
    } else if (event.type?.startsWith('file_') && det.path) {
      detailText = det.path
    } else if (event.type?.startsWith('firewall_') && det.message) {
      detailText = det.message
    } else if (event.type === 'vm_detection') {
      detailText = det.is_vm ? `VM: ${det.reason}` : 'No VM detected'
    } else if (event.type === 'tcp_snapshot') {
      detailText = `${det.count} connections`
    }
  } catch {}

  return (
    <div className={`px-4 py-2.5 flex items-baseline gap-3 text-sm ${event.isFlag ? 'bg-red-950/20' : ''}`}>
      <span className="text-zinc-600 text-xs font-mono shrink-0">{time}</span>
      <span className="text-indigo-400 font-medium shrink-0">{event.studentName}</span>
      <span className={`font-mono text-xs ${typeClass}`}>{event.type}</span>
      {detailText && (
        <span className="text-zinc-500 text-xs truncate">{detailText}</span>
      )}
    </div>
  )
}
