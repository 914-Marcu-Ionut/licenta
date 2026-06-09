import { useState } from 'react'
import { api } from '../api'

export default function Login({ onLogin }) {
  const [name, setName] = useState('')
  const [password, setPassword] = useState('')
  const [error, setError] = useState('')
  const [loading, setLoading] = useState(false)
  const [mode, setMode] = useState('login') // 'login' or 'register'

  async function handleSubmit(e) {
    e.preventDefault()
    if (!name || !password) return setError('Fill in all fields')
    setLoading(true)
    setError('')

    if (mode === 'register') {
      const r = await api('POST', '/register', { name, password })
      if (!r.ok) {
        setError(r.data?.error || 'Registration failed')
        setLoading(false)
        return
      }
    }

    const r = await api('POST', '/login', { name, password })
    setLoading(false)
    if (r.ok) {
      onLogin(r.data, password)
    } else {
      setError(r.data?.error || 'Invalid credentials')
    }
  }

  return (
    <div className="min-h-screen bg-zinc-950 flex items-center justify-center px-4">
      <div className="w-full max-w-sm">
        <h1 className="text-2xl font-bold text-white text-center mb-2">
          Exam Monitor
        </h1>
        <p className="text-zinc-500 text-center text-sm mb-8">
          Teacher portal
        </p>

        <form onSubmit={handleSubmit} className="bg-zinc-900 border border-zinc-800 rounded-xl p-6 space-y-4">
          <div>
            <label className="block text-sm text-zinc-400 mb-1.5">Name</label>
            <input
              type="text"
              value={name}
              onChange={(e) => setName(e.target.value)}
              className="w-full px-3 py-2 bg-zinc-950 border border-zinc-700 rounded-lg text-white text-sm focus:outline-none focus:border-indigo-500 transition-colors"
              placeholder="Your name"
            />
          </div>
          <div>
            <label className="block text-sm text-zinc-400 mb-1.5">Password</label>
            <input
              type="password"
              value={password}
              onChange={(e) => setPassword(e.target.value)}
              className="w-full px-3 py-2 bg-zinc-950 border border-zinc-700 rounded-lg text-white text-sm focus:outline-none focus:border-indigo-500 transition-colors"
              placeholder="Password"
            />
          </div>

          {error && (
            <div className="text-sm text-red-400 bg-red-950/50 border border-red-900 rounded-lg px-3 py-2">
              {error}
            </div>
          )}

          <button
            type="submit"
            disabled={loading}
            className="w-full py-2.5 bg-indigo-600 hover:bg-indigo-500 disabled:opacity-50 text-white text-sm font-medium rounded-lg transition-colors"
          >
            {loading ? '...' : mode === 'login' ? 'Login' : 'Register & Login'}
          </button>

          <button
            type="button"
            onClick={() => setMode(mode === 'login' ? 'register' : 'login')}
            className="w-full text-sm text-zinc-500 hover:text-zinc-300 transition-colors"
          >
            {mode === 'login' ? 'Need an account? Register' : 'Already have an account? Login'}
          </button>
        </form>
      </div>
    </div>
  )
}
