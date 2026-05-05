import React, { useState, useEffect, useRef } from 'react';
import { Database, Search, Plus, Trash2, Activity, Server, ArrowRight } from 'lucide-react';
import './index.css';

const API_URL = 'http://localhost:3001/api';

function App() {
  const [status, setStatus] = useState(null);
  const [logs, setLogs] = useState([]);
  const [key, setKey] = useState('');
  const [val, setVal] = useState('');
  const [loading, setLoading] = useState(false);
  const logEndRef = useRef(null);

  useEffect(() => {
    fetchStatus();
    const interval = setInterval(fetchStatus, 3000);
    return () => clearInterval(interval);
  }, []);

  useEffect(() => {
    logEndRef.current?.scrollIntoView({ behavior: 'smooth' });
  }, [logs]);

  const addLog = (type, text) => {
    setLogs(prev => [...prev, { id: Date.now(), type, text, time: new Date().toLocaleTimeString() }]);
  };

  const fetchStatus = async () => {
    try {
      const res = await fetch(`${API_URL}/status`);
      if (res.ok) {
        const data = await res.json();
        setStatus(data);
      }
    } catch (err) {
      // offline
      setStatus({ role: 'offline' });
    }
  };

  const handleGet = async () => {
    if (!key) return;
    setLoading(true);
    addLog('req', `GET /api/kv/${key}`);
    try {
      const res = await fetch(`${API_URL}/kv/${key}`);
      const data = await res.json();
      if (res.ok && data.found) {
        addLog('success', `Found [${key}]: ${data.value}`);
      } else {
        addLog('error', data.error || 'Key not found');
      }
    } catch (err) {
      addLog('error', err.message);
    }
    setLoading(false);
  };

  const handlePut = async () => {
    if (!key || !val) return;
    setLoading(true);
    addLog('req', `POST /api/kv Body: { key: ${key}, value: ${val} }`);
    try {
      const res = await fetch(`${API_URL}/kv`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ key, value: val })
      });
      const data = await res.json();
      if (res.ok && data.success) {
        addLog('success', `Successfully added/updated key [${key}]`);
        fetchStatus();
      } else {
        addLog('error', data.error || 'Failed to put');
      }
    } catch (err) {
      addLog('error', err.message);
    }
    setLoading(false);
  };

  const handleDelete = async () => {
    if (!key) return;
    setLoading(true);
    addLog('req', `DELETE /api/kv/${key}`);
    try {
      const res = await fetch(`${API_URL}/kv/${key}`, { method: 'DELETE' });
      const data = await res.json();
      if (res.ok && data.success) {
        addLog('info', `Deleted key [${key}]`);
        fetchStatus();
      } else {
        addLog('error', data.error || 'Failed to delete');
      }
    } catch (err) {
      addLog('error', err.message);
    }
    setLoading(false);
  };

  return (
    <div className="dashboard">
      {/* Sidebar / Status */}
      <div className="glass-panel" style={{ display: 'flex', flexDirection: 'column' }}>
        <h2><Activity size={24} color="var(--accent)" /> Cluster Status</h2>
        <div style={{ flex: 1 }}>
          {status && status.role !== 'offline' ? (
            <div className={`status-node ${status.role === 'leader' ? 'leader' : ''}`}>
              <div>
                <div style={{ display: 'flex', alignItems: 'center', gap: '0.5rem' }}>
                  <Server size={16} /> <strong>{status.node_id}</strong>
                </div>
                {status.term && <div className="node-info-text">Term: {status.term}</div>}
              </div>
              <div>
                <span className={`status-badge ${status.role}`}>{status.role}</span>
              </div>
            </div>
          ) : (
             <div className="status-node">
               <div><strong>Gateway</strong></div>
               <span className="status-badge offline">OFFLINE</span>
             </div>
          )}

          {status && status.role !== 'offline' && (
            <div style={{ marginTop: '1.5rem', padding: '1rem', background: 'rgba(0,0,0,0.2)', borderRadius: '8px' }}>
              <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: '0.5rem' }}>
                <span style={{ color: 'var(--text-secondary)' }}>Leader ID</span>
                <strong>{status.leader_id || '-'}</strong>
              </div>
              <div style={{ display: 'flex', justifyContent: 'space-between' }}>
                <span style={{ color: 'var(--text-secondary)' }}>Keys Held</span>
                <strong style={{ color: 'var(--success)' }}>{status.keys_held}</strong>
              </div>
            </div>
          )}
        </div>
        <div style={{ textAlign: 'center', marginTop: '1rem' }}>
          <p className="subtitle" style={{marginBottom: 0}}>Polling Gateway <ArrowRight size={14} style={{verticalAlign: 'middle'}}/> :3001</p>
        </div>
      </div>

      {/* Main Panel */}
      <div className="glass-panel">
        <h2><Database size={24} color="var(--accent)" /> KVStore Explorer</h2>
        <p className="subtitle">Execute read/write operations against the cluster.</p>
        
        <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '1.5rem' }}>
          <div className="form-group">
            <label>Key</label>
            <input 
              type="text" 
              placeholder="e.g. user_session" 
              value={key} 
              onChange={e => setKey(e.target.value)} 
            />
          </div>
          <div className="form-group">
            <label>Value (Optional for GET/DEL)</label>
            <input 
              type="text" 
              placeholder="e.g. 12345" 
              value={val} 
              onChange={e => setVal(e.target.value)} 
            />
          </div>
        </div>

        <div className="btn-row">
          <button className="primary" onClick={handlePut} disabled={loading}>
            {loading ? <span className="loader"></span> : <Plus size={18} />} Insert
          </button>
          <button className="secondary" onClick={handleGet} disabled={loading}>
             <Search size={18} /> Retrieve
          </button>
          <button className="danger" onClick={handleDelete} disabled={loading}>
             <Trash2 size={18} /> Delete
          </button>
        </div>

        <div className="console">
          {logs.length === 0 && <span style={{color: 'var(--text-secondary)'}}>Waiting for operations...</span>}
          {logs.map((log) => (
             <div key={log.id} className={`console-line ${log.type}`}>
               <span style={{color: 'var(--text-secondary)', marginRight: '8px'}}>[{log.time}]</span>
               {log.text}
             </div>
          ))}
          <div ref={logEndRef} />
        </div>
      </div>
    </div>
  );
}

export default App;
