// ============================================================================
// Agent Ops Center — server (Node 20+, express + socket.io)
// v2: Arabic-first UI, site-side GH_PAT control, full commits feed.
// ============================================================================
'use strict';
const express = require('express');
const http = require('http');
const fs = require('fs');
const path = require('path');
const os = require('os');
const crypto = require('crypto');
const { exec } = require('child_process');
const { Server } = require('socket.io');

const PORT = Number(process.env.PORT || 8080);
const CODE = process.env.ACCESS_CODE || 'erbi2025';
const OWNER = process.env.GH_OWNER || 'abdallh-rgx';
const REPO = process.env.GH_REPO || 'idk';
let PAT = process.env.PAT_TOKEN || '';
const HOME = os.homedir();
const ROOT = path.resolve(__dirname, '..');
const SRC_DIR = path.join(HOME, 'fortnite-src');
const REPO_DIR = path.join(HOME, 'idk');
const STATE_FILE = '/tmp/agent-ops-state.json';
const MF_LOG = path.join(HOME, 'mf.log');
const TUNNEL_LOG = path.join(HOME, 'tunnel.log');
const SERVER_LOG = path.join(HOME, 'server.log');

const CHANNELS = ['general', 'interaction', 'quickbars', 'loot', 'vehicles', 'building', 'inventory', 'crash-debug', 're-tools', 'builds'];

const state = {
  agents: [], messages: [], notifications: [], commits: [],
  tunnel: '', tunnelIssue: null, bootedAt: Date.now(),
};
try { const s = JSON.parse(fs.readFileSync(STATE_FILE, 'utf8')); Object.assign(state, s); if (s && s.pat) PAT = s.pat; } catch {}
const save = () => { try { fs.writeFileSync(STATE_FILE, JSON.stringify({ ...state, pat: PAT })); } catch (e) { console.error('save failed', e.message); } };
const uid = () => crypto.randomBytes(6).toString('hex');

const app = express();
app.use(express.json({ limit: '40mb' }));
const server = http.createServer(app);
const io = new Server(server, { cors: { origin: '*' }, maxHttpBufferSize: 40e6 });

const bcast = (ev, data) => { try { io.emit(ev, data); } catch {} };
const log = (...a) => { const line = `[${new Date().toISOString()}] ${a.join(' ')}`; console.log(line); try { fs.appendFileSync(SERVER_LOG, line + '\n'); } catch {} };

function auth(req, res, next) {
  const c = req.headers['x-code'] || req.query.code || (req.body && req.body.code);
  if (c !== CODE) return res.status(401).json({ error: 'bad access code' });
  next();
}

function agentById(id) { return state.agents.find(a => a.id === id); }
function touchAgent(id) { const a = agentById(id); if (a) a.lastPing = Date.now(); }
const ONLINE_MS = 3 * 60 * 1000;
const ACTIVE_MS = 15 * 60 * 1000;
const agentView = a => ({ ...a, online: Date.now() - a.lastPing < ONLINE_MS, active: Date.now() - a.lastPing < ACTIVE_MS });

function activeApproverIds(exceptId) {
  return state.agents.filter(a => a.id !== exceptId && Date.now() - a.lastPing < ACTIVE_MS).map(a => a.id);
}

// ---------------- approval engine ----------------
function createNotification({ type, label, messageId = null, action = null, authorId }) {
  const n = {
    id: uid(), type, label, messageId, action,
    status: 'pending', createdAt: Date.now(),
    required: activeApproverIds(authorId), votes: [], result: null,
  };
  state.notifications.push(n);
  if (n.required.length === 0) {
    n.status = 'approved';
    n.result = 'auto-approved (solo mode)';
    executeIfAction(n);
  } else {
    bcast('notification:new', n);
  }
  save();
  return n;
}

function vote(n, agentId, decision) {
  if (n.status !== 'pending') return { ok: false, error: 'already ' + n.status };
  if (!n.required.includes(agentId)) {
    const v = n.votes.find(v => v.agentId === agentId);
    if (v) v.decision = decision; else n.votes.push({ agentId, decision, at: Date.now(), advisory: true });
    return { ok: true, advisory: true };
  }
  const v = n.votes.find(v => v.agentId === agentId);
  if (v) v.decision = decision; else n.votes.push({ agentId, decision, at: Date.now() });
  if (n.votes.some(v => v.decision === 'reject' && n.required.includes(v.agentId))) {
    n.status = 'rejected'; bcast('notification:updated', n); save();
    if (n.messageId) syncMessageStatus(n.messageId);
    return { ok: true, status: n.status };
  }
  const approved = n.votes.filter(v => v.decision === 'approve' && n.required.includes(v.agentId)).map(v => v.agentId);
  if (n.required.every(id => approved.includes(id))) {
    n.status = 'approved';
    executeIfAction(n);
    bcast('notification:updated', n); save();
    if (n.messageId) syncMessageStatus(n.messageId);
    return { ok: true, status: 'approved' };
  }
  bcast('notification:updated', n); save();
  return { ok: true, status: n.status };
}

function recordCommit(entry) {
  const c = { id: uid(), at: Date.now(), ...entry };
  state.commits.unshift(c); state.commits = state.commits.slice(0, 100);
  bcast('commit:new', c); save();
}

function executeIfAction(n) {
  if (!n.action) return;
  const a = n.action;
  try {
    if (a.type === 'command') {
      exec(a.cmd, { cwd: a.cwd || ROOT, timeout: (a.timeout || 180) * 1000, maxBuffer: 16 * 1024 * 1024 }, (err, stdout, stderr) => {
        n.result = { cmd: a.cmd, code: err ? (err.code ?? 1) : 0, stdout: (stdout || '').slice(0, 400000), stderr: (stderr || (err && err.message) || '').slice(0, 100000) };
        bcast('notification:updated', n); save();
      });
    } else if (a.type === 'commit') {
      pushFileToGitHub(a.path, a.content, a.message).then(r => {
        n.result = r;
        recordCommit({ path: a.path, message: a.message, by: a.byName || 'approval-flow', sha: (r.commit || '').slice(0, 7), ok: r.ok });
        bcast('notification:updated', n); save();
      }).catch(e => { n.result = { error: e.message }; bcast('notification:updated', n); save(); });
    }
  } catch (e) { n.result = { error: e.message }; }
}

function syncMessageStatus(messageId) {
  const m = state.messages.find(m => m.id === messageId);
  if (!m) return;
  const n = state.notifications.find(x => x.messageId === messageId);
  if (n && ['approved', 'rejected'].includes(n.status)) m.status = n.status;
  bcast('message:updated', m); save();
}

// ---------------- GitHub API ----------------
async function gh(apiPath, opts = {}) {
  if (!PAT) throw new Error('no PAT configured (set via site Settings or PAT_TOKEN secret)');
  const r = await fetch(`https://api.github.com${apiPath}`, {
    ...opts,
    headers: {
      Authorization: `token ${PAT}`,
      Accept: 'application/vnd.github+json',
      'User-Agent': 'agent-ops',
      ...(opts.headers || {}),
    },
  });
  if (!r.ok) throw new Error(`GH ${r.status}: ${(await r.text()).slice(0, 500)}`);
  const ct = r.headers.get('content-type') || '';
  return ct.includes('json') ? r.json() : r.text();
}

async function findOrCreateTunnelIssue() {
  try {
    const issues = await gh(`/repos/${OWNER}/${REPO}/issues?state=open&per_page=50`);
    let issue = issues.find(i => i.title.includes('Tunnel URL') && i.user.login === OWNER);
    if (!issue) {
      issue = await gh(`/repos/${OWNER}/${REPO}/issues`, {
        method: 'POST', body: JSON.stringify({
          title: '🌐 Tunnel URL (auto-updated by agent-ops)',
          body: 'Latest Ops Center tunnel URL. Do not close this issue.',
        }),
      });
    }
    state.tunnelIssue = issue.number; save();
    return issue.number;
  } catch (e) { log('tunnel issue error:', e.message); return null; }
}

async function postIssueComment(body) {
  const num = state.tunnelIssue || await findOrCreateTunnelIssue();
  if (!num) throw new Error('no tunnel issue');
  return gh(`/repos/${OWNER}/${REPO}/issues/${num}/comments`, { method: 'POST', body: JSON.stringify({ body }) });
}

async function pushFileToGitHub(filePath, content, message, branch = 'main') {
  let sha = undefined;
  try {
    const existing = await gh(`/repos/${OWNER}/${REPO}/contents/${encodeURIComponent(filePath)}?ref=${branch}`);
    sha = existing.sha;
  } catch {}
  return gh(`/repos/${OWNER}/${REPO}/contents/${encodeURIComponent(filePath)}`, {
    method: 'PUT',
    body: JSON.stringify({
      message: message || `agent-ops: update ${filePath}`,
      content: Buffer.from(content, 'utf8').toString('base64'),
      branch, ...(sha ? { sha } : {}),
    }),
  }).then(r => ({ ok: true, commit: r.commit && r.commit.sha, path: filePath }));
}

// ---------------- boot ----------------
log('=== Agent Ops Center v2 booting ===');
function cloneIdk() {
  if (!PAT) return log('skip idk clone (no PAT yet)');
  exec(`git clone --depth 50 https://x-access-token:${PAT}@github.com/${OWNER}/${REPO}.git ${REPO_DIR} 2>&1 || (cd ${REPO_DIR} && git pull 2>&1)`,
    { timeout: 120000 }, (err, stdout) => log('idk clone:', (stdout || (err && err.message) || '').slice(-300)));
}
cloneIdk();
if (PAT) findOrCreateTunnelIssue();

// ---------------- meta ----------------
app.get('/api/health', (req, res) => {
  const has = p => { try { return fs.existsSync(p); } catch { return false; } };
  res.json({
    ok: true, tunnel: state.tunnel, uptimeSec: Math.floor((Date.now() - state.bootedAt) / 1000),
    agents: state.agents.length, pat: !!PAT,
    files: {
      srcZip: has(path.join(SRC_DIR, 'src.zip')),
      libUnreal: has(path.join(SRC_DIR, 'libUnreal.so')) || has(path.join(SRC_DIR, 'src', 'libUnreal.so')),
      idkRepo: has(path.join(REPO_DIR, '.git')),
    },
  });
});
app.get('/api/auth', auth, (req, res) => res.json({ ok: true }));
app.get('/api/state', auth, (req, res) => res.json({
  channels: CHANNELS, tunnel: state.tunnel, agents: state.agents.map(agentView),
  messages: state.messages, notifications: state.notifications, commits: state.commits,
  pat: !!PAT, owner: OWNER, repo: REPO,
}));
app.post('/api/tunnel', (req, res) => {
  const url = String(req.body.url || '').trim();
  if (!url.includes('trycloudflare.com') && !url.includes('localhost')) return res.status(400).json({ error: 'bad url' });
  state.tunnel = url; save();
  log('tunnel set:', url);
  if (PAT && !url.includes('localhost')) {
    postIssueComment(`🌐 **${url}**\n_Updated: ${new Date().toISOString()}_`).then(() => log('issue comment posted')).catch(e => log('issue comment failed:', e.message));
  }
  bcast('tunnel:set', url);
  res.json({ ok: true, tunnel: url });
});

// ---------------- settings (site-side PAT control) ----------------
app.get('/api/settings', auth, (req, res) => res.json({ pat: !!PAT, owner: OWNER, repo: REPO }));
app.post('/api/settings', auth, (req, res) => {
  const newPat = String(req.body.pat || '').trim();
  if (newPat && !/^gh[pousr]_[A-Za-z0-9]{30,}$/.test(newPat)) return res.status(400).json({ error: 'PAT format looks wrong' });
  if (newPat) {
    PAT = newPat; save();
    cloneIdk();
    findOrCreateTunnelIssue().catch(() => {});
    log('PAT updated via site settings');
  }
  res.json({ ok: true, pat: !!PAT });
});

// ---------------- agents ----------------
app.get('/api/agents', auth, (req, res) => res.json(state.agents.map(agentView)));
app.post('/api/agents', auth, (req, res) => {
  const name = String(req.body.name || '').trim().slice(0, 60);
  if (!name) return res.status(400).json({ error: 'name required' });
  let a = state.agents.find(a => a.name.toLowerCase() === name.toLowerCase());
  if (!a) { a = { id: uid(), name, role: String(req.body.role || 'agent').slice(0, 40), createdAt: Date.now(), lastPing: Date.now() }; state.agents.push(a); }
  else { a.role = req.body.role || a.role; a.lastPing = Date.now(); }
  save(); bcast('agents:update', state.agents.map(agentView));
  res.json(a);
});
app.post('/api/agents/:id/ping', auth, (req, res) => { touchAgent(req.params.id); save(); res.json({ ok: true }); });

// ---------------- chat + strict approvals ----------------
app.get('/api/messages', auth, (req, res) => {
  let list = state.messages;
  if (req.query.channel) list = list.filter(m => m.channel === req.query.channel);
  if (req.query.since) list = list.filter(m => m.createdAt > Number(req.query.since));
  res.json(list.slice(-400));
});

app.post('/api/messages', auth, (req, res) => {
  const { channel, content, kind, authorId, payload } = req.body;
  const author = agentById(authorId);
  if (!author) return res.status(400).json({ error: 'unknown authorId — register first' });
  if (!CHANNELS.includes(channel)) return res.status(400).json({ error: 'bad channel' });
  const text = String(content || '').slice(0, 20000);
  if (!text) return res.status(400).json({ error: 'content required' });
  touchAgent(authorId);

  const k = ['chat', 'command', 'commit', 'request'].includes(kind) ? kind : 'chat';
  const m = {
    id: uid(), channel, authorId, authorName: author.name,
    content: text, kind: k, payload: k === 'chat' ? null : (payload || null),
    createdAt: Date.now(), status: 'pending', required: [], votes: [], result: null,
  };
  state.messages.push(m);

  const n = createNotification({
    type: k, label: k === 'chat' ? `💬 ${author.name} في #${channel}` : `${k.toUpperCase()} اقتراح من ${author.name}`,
    messageId: m.id, authorId,
    action: k === 'command' ? { type: 'command', cmd: (payload && payload.cmd) || text, cwd: payload && payload.cwd, timeout: payload && payload.timeout }
      : k === 'commit' ? { type: 'commit', path: payload && payload.path, content: payload && payload.content, message: payload && payload.message, byName: author.name }
      : null,
  });
  m.required = n.required; m.votes = n.votes; m.status = n.status;
  bcast('message:new', m); save();
  res.json({ message: m, notification: n });
});

app.post('/api/messages/:id/vote', auth, (req, res) => {
  const m = state.messages.find(x => x.id === req.params.id);
  if (!m) return res.status(404).json({ error: 'not found' });
  const n = state.notifications.find(x => x.messageId === m.id);
  if (!n) return res.status(500).json({ error: 'no notification' });
  touchAgent(req.body.agentId);
  const r = vote(n, req.body.agentId, req.body.decision);
  m.votes = n.votes; m.required = n.required;
  if (['approved', 'rejected'].includes(n.status)) m.status = n.status;
  if (n.result) m.result = n.result;
  bcast('message:updated', m); save();
  res.json(r);
});

app.get('/api/notifications', auth, (req, res) => {
  let list = state.notifications;
  if (req.query.status) list = list.filter(n => n.status === req.query.status);
  res.json(list.slice(-200));
});
app.post('/api/notifications/:id/vote', auth, (req, res) => {
  const n = state.notifications.find(x => x.id === req.params.id);
  if (!n) return res.status(404).json({ error: 'not found' });
  touchAgent(req.body.agentId);
  const r = vote(n, req.body.agentId, req.body.decision);
  if (n.messageId) syncMessageStatus(n.messageId);
  res.json(r);
});

// ---------------- commits feed (site commits + GitHub commits) ----------------
app.get('/api/commits', auth, async (req, res) => {
  try {
    const list = await gh(`/repos/${OWNER}/${REPO}/commits?per_page=${req.query.per || 30}`);
    const ghCommits = list.map(c => ({ id: c.sha, sha: c.sha.slice(0, 7), msg: (c.commit.message || '').split('\n')[0].slice(0, 140), author: (c.commit.author || {}).name, date: c.commit.author && c.commit.author.date, source: 'github' }));
    res.json([...state.commits, ...ghCommits].slice(0, 80));
  } catch (e) {
    res.json(state.commits);
  }
});

// ---------------- terminal ----------------
app.post('/api/exec', auth, (req, res) => {
  const cmd = String(req.body.cmd || '');
  if (!cmd.trim()) return res.status(400).json({ error: 'cmd required' });
  const timeout = Math.min(Number(req.body.timeout || 180), 600) * 1000;
  exec(cmd, { cwd: req.body.cwd || ROOT, timeout, maxBuffer: 16 * 1024 * 1024, shell: '/bin/bash' }, (err, stdout, stderr) => {
    res.json({
      cmd, code: err ? (err.code ?? 1) : 0,
      stdout: (stdout || '').slice(0, 400000),
      stderr: (stderr || (err && err.message) || '').slice(0, 100000),
    });
  });
});

// ---------------- filesystem ----------------
const safePath = p => path.resolve(String(p || HOME));
app.get('/api/fs/ls', auth, (req, res) => {
  const dir = safePath(req.query.path);
  fs.readdir(dir, { withFileTypes: true }, (err, entries) => {
    if (err) return res.status(400).json({ error: err.message });
    const out = [];
    for (const e of entries.slice(0, 2000)) {
      try {
        const st = fs.statSync(path.join(dir, e.name));
        out.push({ name: e.name, dir: e.isDirectory(), size: st.size, mtime: st.mtimeMs });
      } catch { out.push({ name: e.name, dir: e.isDirectory(), size: 0, mtime: 0 }); }
    }
    out.sort((a, b) => (b.dir - a.dir) || a.name.localeCompare(b.name));
    res.json({ path: dir, entries: out });
  });
});
app.get('/api/fs/read', auth, (req, res) => {
  const p = safePath(req.query.path);
  const bytes = Math.min(Number(req.query.bytes || 300000), 2000000);
  fs.readFile(p, (err, buf) => {
    if (err) return res.status(400).json({ error: err.message });
    const slice = buf.slice(0, bytes);
    const binary = slice.includes(0);
    res.json({ path: p, size: buf.length, truncated: buf.length > bytes, binary, text: binary ? slice.toString('hex').slice(0, 4000) : slice.toString('utf8') });
  });
});
app.post('/api/fs/write', auth, (req, res) => {
  const p = safePath(req.body.path);
  fs.writeFile(p, String(req.body.content || ''), 'utf8', err => err ? res.status(400).json({ error: err.message }) : res.json({ ok: true, path: p }));
});
app.get('/api/fs/download', auth, (req, res) => {
  const p = safePath(req.query.path);
  if (!fs.existsSync(p)) return res.status(404).json({ error: 'not found' });
  res.download(p);
});

// ---------------- logs ----------------
app.get('/api/log/:name', auth, (req, res) => {
  const map = { mf: MF_LOG, tunnel: TUNNEL_LOG, server: SERVER_LOG };
  const p = map[req.params.name];
  if (!p) return res.status(400).json({ error: 'unknown log' });
  exec(`tail -c 60000 "${p}" 2>/dev/null || echo "(no log yet)"`, { timeout: 10000 }, (err, stdout) => res.json({ log: stdout || '' }));
});

// ---------------- MediaFire ----------------
app.post('/api/mf/download', auth, (req, res) => {
  const url = String(req.body.url || '').trim();
  if (!url.includes('mediafire.com')) return res.status(400).json({ error: 'mediafire url required' });
  exec(`pkill -f dl_mediafire.sh 2>/dev/null; nohup bash "${ROOT}/scripts/dl_mediafire.sh" "${url}" "${SRC_DIR}" >> "${MF_LOG}" 2>&1 & echo started`,
    { timeout: 10000 }, (err, stdout) => res.json({ ok: !err, started: true, out: stdout }));
});
app.get('/api/mf/status', auth, (req, res) => {
  exec(`tail -c 8000 "${MF_LOG}" 2>/dev/null; echo "---FILES---"; ls -la "${SRC_DIR}" 2>/dev/null | head -30`, { timeout: 10000 }, (err, stdout) => res.json({ status: stdout || '(nothing yet)' }));
});

// ---------------- git local ----------------
const gitCmd = (args, res) => exec(`git -C "${REPO_DIR}" ${args}`, { timeout: 60000, maxBuffer: 8 * 1024 * 1024 }, (err, stdout, stderr) =>
  res.json({ code: err ? (err.code ?? 1) : 0, out: stdout, err: stderr || (err && err.message) || '' }));
app.get('/api/git/status', auth, (req, res) => gitCmd('status --short --branch && echo --- && git -C "' + REPO_DIR + '" log --oneline -5', res));
app.post('/api/git/pull', auth, (req, res) => gitCmd('pull --rebase', res));
app.post('/api/git/push', auth, (req, res) => gitCmd('push origin HEAD', res));
app.get('/api/git/diff', auth, (req, res) => gitCmd('diff | head -c 100000', res));

// ---------------- GitHub (idk control) ----------------
app.get('/api/gh/runs', auth, async (req, res) => {
  try {
    const r = await gh(`/repos/${OWNER}/${REPO}/actions/runs?per_page=${req.query.per || 15}`);
    res.json(r.workflow_runs.map(x => ({ id: x.id, name: x.name, status: x.status, conclusion: x.conclusion, event: x.event, created: x.created_at, url: x.html_url })));
  } catch (e) { res.status(500).json({ error: e.message }); }
});
app.get('/api/gh/workflows', auth, async (req, res) => {
  try {
    const r = await gh(`/repos/${OWNER}/${REPO}/actions/workflows`);
    res.json(r.workflows.map(w => ({ id: w.id, name: w.name, path: w.path, state: w.state })));
  } catch (e) { res.status(500).json({ error: e.message }); }
});
app.post('/api/gh/dispatch', auth, async (req, res) => {
  try {
    const { workflow, ref, inputs } = req.body;
    await gh(`/repos/${OWNER}/${REPO}/actions/workflows/${workflow || 'host.yml'}/dispatches`, {
      method: 'POST', body: JSON.stringify({ ref: ref || 'main', inputs: inputs || {} }),
    });
    res.json({ ok: true, dispatched: workflow || 'host.yml' });
  } catch (e) { res.status(500).json({ error: e.message }); }
});

io.on('connection', socket => {
  socket.emit('hello', { tunnel: state.tunnel, agents: state.agents.map(agentView) });
});

app.use(express.static(path.join(__dirname, 'public')));
app.get('/', (req, res) => res.sendFile(path.join(__dirname, 'public', 'index.html')));

server.listen(PORT, () => log(`Agent Ops Center v2 listening on :${PORT} (PAT=${!!PAT}, code set=${!!CODE})`));
