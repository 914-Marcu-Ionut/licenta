import { api } from '../api'

export default function StudentGrid({ students, onSelect, onClearFlags }) {
  const entries = Object.entries(students)

  if (entries.length === 0) {
    return (
      <div className="text-sm text-zinc-600 text-center py-4">
        No students registered yet.
      </div>
    )
  }

  return (
    <div className="grid grid-cols-2 sm:grid-cols-3 md:grid-cols-4 lg:grid-cols-5 xl:grid-cols-6 gap-3">
      {entries.map(([id, student]) => (
        <StudentCard key={id} id={id} student={student} onClick={() => onSelect(id)} onClear={() => onClearFlags(id)} />
      ))}
    </div>
  )
}

function StudentCard({ id, student, onClick, onClear }) {
  const flags = student.flags || []
  const level = getLevel(flags)

  function handleClear(e) {
    e.stopPropagation()
    onClear()
  }

  return (
    <div
      onClick={onClick}
      className={`relative rounded-xl p-4 text-left transition-all bg-zinc-900 hover:bg-zinc-800/80 border-3 cursor-pointer ${level.borderClass}`}
      style={level.flash ? { animation: level.animation } : undefined}
    >
      <div className="flex items-center justify-between mb-1">
        <span className="text-sm font-medium text-white truncate">
          {student.name || id}
        </span>
        <span className={`flex items-center gap-1 text-[10px] font-medium shrink-0 ${student.connected ? 'text-emerald-400' : 'text-amber-400'}`}>
          <div className={`w-2 h-2 rounded-full ${student.connected ? 'bg-emerald-500' : 'bg-amber-500 animate-pulse'}`} />
          {student.connected ? 'ON' : 'OFF'}
        </span>
      </div>

      {flags.length > 0 ? (
        <div className="flex items-center justify-between mt-2">
          <span className={`text-xs font-semibold ${level.textClass}`}>
            {flags.length} flag{flags.length !== 1 ? 's' : ''}
          </span>
          <button
            onClick={handleClear}
            className="px-2 py-0.5 text-[10px] font-medium bg-zinc-800 hover:bg-zinc-700 text-zinc-400 hover:text-white rounded transition-colors"
          >
            Clear
          </button>
        </div>
      ) : (
        <span className="text-xs text-zinc-600 mt-2 block">No flags</span>
      )}
    </div>
  )
}

function getLevel(flags) {
  if (flags.length === 0) {
    return {
      borderClass: 'border-emerald-600',
      textClass: 'text-emerald-400',
      flash: false,
      animation: '',
    }
  }

  const hasHigh = flags.some(f => {
    const highTypes = ['firewall_tampered', 'virtual_machine', 'suspicious_connection',
      'chain_broken', 'chain_seq_mismatch', 'chain_hash_mismatch', 'usb_connected',
      'session_logoff', 'shutdown_attempt', 'file_copy_outside_env']
    return highTypes.includes(f.type)
  })

  if (hasHigh) {
    return {
      borderClass: 'border-red-500',
      textClass: 'text-red-400',
      flash: true,
      animation: 'pulse-red 1.2s ease-in-out infinite',
    }
  }

  return {
    borderClass: 'border-amber-500',
    textClass: 'text-amber-400',
    flash: true,
    animation: 'pulse-amber 1.8s ease-in-out infinite',
  }
}
