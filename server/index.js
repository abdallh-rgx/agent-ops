// ============================================================================
// Agent Ops Center — server v3 (adds full Git/PR system)
// Node 20+, express + socket.io
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
  agents: [], messages: [], notifications: [], commits: [], prs: [],
  tunnel: '', tunnelIssue: null, bootedAt: Date.now(),
};
try {
  const s = JSON.parse(fs.readFileSync(STATE_FILE, 'utf8'));
  Object.assign(state, s);
  if (s && s.pat) PAT = s.pat;
  if (!state.prs) state.prs = [];
} catch {}
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

// ---------------- approval engine (legacy messages/notifications) ----------------
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

// ============================================================================
// ═══════════════════════ PULL REQUESTS ENGINE ═══════════════════════════
// ============================================================================
function prById(id) { return state.prs.find(p => p.id === id); }

// Strip heavy file content before broadcasting
function prSummary(pr) {
  return {
    ...pr,
    files: (pr.files || []).map(f => ({ path: f.path, additions: f.additions || 0, deletions: f.deletions || 0 })),
  };
}

// Simple unified-diff generator (LCS-based, hunks with 3 lines of context)
function makeUnifiedDiff(oldStr, newStr, filePath) {
  const a = (oldStr || '').split('\n');
  const b = (newStr || '').split('\n');
  const n = a.length, m = b.length;
  let ops;
  if (n * m > 500000) {
    ops = [];
    for (const l of a) ops.push({ t: '-', l });
    for (const l of b) ops.push({ t: '+', l });
  } else {
    const dp = Array.from({ length: n + 1 }, () => new Uint32Array(m + 1));
    for (let i = 1; i <= n; i++) {
      for (let j = 1; j <= m; j++) {
        dp[i][j] = a[i - 1] === b[j - 1] ? dp[i - 1][j - 1] + 1 : Math.max(dp[i - 1][j], dp[i][j - 1]);
      }
    }
    ops = [];
    let i = n, j = m;
    while (i > 0 || j > 0) {
      if (i > 0 && j > 0 && a[i - 1] === b[j - 1]) { ops.push({ t: ' ', l: a[i - 1] }); i--; j--; }
      else if (j > 0 && (i === 0 || dp[i][j - 1] >= dp[i - 1][j])) { ops.push({ t: '+', l: b[j - 1] }); j--; }
      else { ops.push({ t: '-', l: a[i - 1] }); i--; }
    }
    ops.reverse();
  }
  const changeIdxs = [];
  for (let k = 0; k < ops.length; k++) if (ops[k].t !== ' ') changeIdxs.push(k);
  if (!changeIdxs.length) return '';

  const ctx = 3;
  const groups = [];
  let gs = Math.max(0, changeIdxs[0] - ctx);
  let ge = Math.min(ops.length - 1, changeIdxs[0] + ctx);
  for (let c = 1; c < changeIdxs.length; c++) {
    const idx = changeIdxs[c];
    if (idx - ctx <= ge + 1) ge = Math.min(ops.length - 1, idx + ctx);
    else { groups.push([gs, ge]); gs = Math.max(0, idx - ctx); ge = Math.min(ops.length - 1, idx + ctx); }
  }
  groups.push([gs, ge]);

  const hunks = [];
  for (const [s, e] of groups) {
    let oS = 1, nS = 1;
    for (let k = 0; k < s; k++) { if (ops[k].t !== '+') oS++; if (ops[k].t !== '-') nS++; }
    let oCnt = 0, nCnt = 0;
    const body = [];
    for (let k = s; k <= e; k++) {
      body.push(ops[k].t + ops[k].l);
      if (ops[k].t !== '+') oCnt++;
      if (ops[k].t !== '-') nCnt++;
    }
    hunks.push(`@@ -${oS},${oCnt} +${nS},${nCnt} @@\n` + body.join('\n'));
  }
  return `--- a/${filePath}\n+++ b/${filePath}\n` + hunks.join('\n') + '\n';
}

function countDiffLines(oldStr, newStr) {
  const diff = makeUnifiedDiff(oldStr || '', newStr || '', 'x');
  let a = 0, d = 0;
  for (const line of diff.split('\n')) {
    if (line.startsWith('+') && !line.startsWith('+++')) a++;
    else if (line.startsWith('-') && !line.startsWith('---')) d++;
  }
  return { additions: a, deletions: d };
}

function safeRepoPath(rel) {
  const clean = String(rel || '').replace(/^\/+/, '');
  const abs = path.resolve(REPO_DIR, clean);
  if (!abs.startsWith(REPO_DIR)) throw new Error('path escapes repo');
  return { rel: clean, abs };
}

async function applyFilesToRepo(files) {
  for (const f of files) {
    const { abs } = safeRepoPath(f.path);
    fs.mkdirSync(path.dirname(abs), { recursive: true });
    fs.writeFileSync(abs, String(f.content || ''), 'utf8');
  }
}

function commitAndPush(title) {
  return new Promise(resolve => {
    const safeTitle = String(title).replace(/["`$\\]/g, "'").slice(0, 200);
    const cmd = `cd "${REPO_DIR}" && git add -A && git -c user.name="Agent Ops" -c user.email="agent-ops@local" commit -m "${safeTitle}" && git push origin HEAD 2>&1`;
    exec(cmd, { timeout: 120000, maxBuffer: 8 * 1024 * 1024 }, (err, stdout, stderr) => {
      resolve({ ok: !err, out: stdout || '', err: stderr || (err && err.message) || '', code: err ? (err.code ?? 1) : 0 });
    });
  });
}

// ─── Repo file browser ───
app.get('/api/repo/files', auth, (req, res) => {
  try {
    const { rel, abs } = safeRepoPath(req.query.path || '.');
    fs.readdir(abs, { withFileTypes: true }, (err, entries) => {
      if (err) return res.status(400).json({ error: err.message });
      const out = entries
        .filter(e => e.name !== '.git')
        .map(e => {
          const child = path.join(abs, e.name);
          let size = 0;
          try { size = fs.statSync(child).size; } catch {}
          return { name: e.name, dir: e.isDirectory(), size, path: path.relative(REPO_DIR, child).split(path.sep).join('/') };
        })
        .sort((x, y) => (y.dir - x.dir) || x.name.localeCompare(y.name));
      res.json({ path: rel, entries: out });
    });
  } catch (e) { res.status(400).json({ error: e.message }); }
});

app.get('/api/repo/file', auth, (req, res) => {
  try {
    const { rel, abs } = safeRepoPath(req.query.path);
    if (!rel) return res.status(400).json({ error: 'path required' });
    const content = fs.readFileSync(abs, 'utf8');
    res.json({ path: rel, content, size: content.length });
  } catch (e) { res.status(400).json({ error: e.message }); }
});

app.post('/api/repo/file', auth, (req, res) => {
  try {
    const { rel, abs } = safeRepoPath(req.body.path);
    if (!rel) return res.status(400).json({ error: 'path required' });
    fs.mkdirSync(path.dirname(abs), { recursive: true });
    fs.writeFileSync(abs, String(req.body.content || ''), 'utf8');
    res.json({ ok: true, path: rel });
  } catch (e) { res.status(400).json({ error: e.message }); }
});

// ─── PRs CRUD ───
app.get('/api/prs', auth, (req, res) => {
  res.json(state.prs.slice().reverse().map(prSummary));
});

app.get('/api/prs/:id', auth, (req, res) => {
  const pr = prById(req.params.id);
  if (!pr) return res.status(404).json({ error: 'not found' });
  res.json(prSummary(pr));
});

app.post('/api/prs', auth, (req, res) => {
  const { title, description, files, authorId, authorName } = req.body;
  const author = agentById(authorId);
  if (!author) return res.status(400).json({ error: 'unknown authorId — register first' });
  if (!title || !String(title).trim()) return res.status(400).json({ error: 'title required' });
  if (!Array.isArray(files) || !files.length) return res.status(400).json({ error: 'files required' });
  touchAgent(authorId);

  // Validate paths and compute stats
  const processed = [];
  for (const f of files) {
    if (!f || !f.path) return res.status(400).json({ error: 'each file needs path' });
    try { safeRepoPath(f.path); } catch (e) { return res.status(400).json({ error: e.message }); }
    const { additions, deletions } = countDiffLines(f.baseContent || '', f.content || '');
    processed.push({
      path: String(f.path).replace(/^\/+/, ''),
      content: String(f.content || ''),
      baseContent: String(f.baseContent || ''),
      additions, deletions,
    });
  }

  const required = activeApproverIds(authorId);
  const pr = {
    id: uid(),
    title: String(title).slice(0, 200),
    description: String(description || '').slice(0, 5000),
    files: processed,
    authorId, authorName: author.name || authorName || '?',
    createdAt: Date.now(),
    status: required.length === 0 ? 'approved' : 'open',
    required, votes: [], comments: [],
  };
  state.prs.push(pr);
  bcast('pr:new', prSummary(pr));
  save();
  log(`PR created by ${pr.authorName}: ${pr.title} (status=${pr.status})`);
  res.json(prSummary(pr));
});

app.get('/api/prs/:id/diff', auth, (req, res) => {
  const pr = prById(req.params.id);
  if (!pr) return res.status(404).json({ error: 'not found' });
  const idx = Number(req.query.file || 0);
  const f = pr.files[idx];
  if (!f) return res.status(400).json({ error: 'bad file index' });
  const diff = makeUnifiedDiff(f.baseContent || '', f.content || '', f.path);
  res.json({ path: f.path, diff: diff || '(no diff — identical)' });
});

app.post('/api/prs/:id/vote', auth, (req, res) => {
  const pr = prById(req.params.id);
  if (!pr) return res.status(404).json({ error: 'not found' });
  if (!['open', 'changes_requested', 'approved'].includes(pr.status)) {
    return res.status(400).json({ error: 'PR is ' + pr.status });
  }
  const { agentId, decision, comment } = req.body;
  if (!['approve', 'request_changes', 'reject'].includes(decision)) {
    return res.status(400).json({ error: 'bad decision' });
  }
  const agent = agentById(agentId);
  if (!agent) return res.status(400).json({ error: 'unknown agent' });
  touchAgent(agentId);

  // New active agent voting → add to required
  if (agentId !== pr.authorId && Date.now() - agent.lastPing < ACTIVE_MS && !pr.required.includes(agentId)) {
    pr.required.push(agentId);
  }

  const existing = pr.votes.find(v => v.agentId === agentId);
  if (existing) {
    existing.decision = decision;
    existing.comment = String(comment || '').slice(0, 2000);
    existing.at = Date.now();
  } else {
    pr.votes.push({ agentId, decision, comment: String(comment || '').slice(0, 2000), at: Date.now() });
  }

  // Recompute status
  const requiredVotes = pr.votes.filter(v => pr.required.includes(v.agentId));
  if (requiredVotes.some(v => v.decision === 'reject')) {
    pr.status = 'rejected';
  } else if (requiredVotes.some(v => v.decision === 'request_changes')) {
    pr.status = 'changes_requested';
  } else {
    const approvedIds = new Set(requiredVotes.filter(v => v.decision === 'approve').map(v => v.agentId));
    pr.status = pr.required.every(id => approvedIds.has(id)) ? 'approved' : 'open';
  }

  bcast('pr:updated', prSummary(pr));
  save();
  log(`PR ${pr.id.slice(0, 6)} vote: ${agent.name} → ${decision} (status=${pr.status})`);
  res.json({ ok: true, pr: prSummary(pr) });
});

app.post('/api/prs/:id/comment', auth, (req, res) => {
  const pr = prById(req.params.id);
  if (!pr) return res.status(404).json({ error: 'not found' });
  const { agentId, name, content } = req.body;
  const text = String(content || '').trim();
  if (!text) return res.status(400).json({ error: 'content required' });
  if (agentId) touchAgent(agentId);
  const agent = agentById(agentId);
  const comment = {
    id: uid(),
    agentId: agentId || null,
    name: (agent && agent.name) || name || '?',
    content: text.slice(0, 5000),
    at: Date.now(),
  };
  pr.comments.push(comment);
  bcast('pr:comment', { prId: pr.id, comment });
  save();
  res.json({ ok: true, comment });
});

app.post('/api/prs/:id/merge', auth, async (req, res) => {
  const pr = prById(req.params.id);
  if (!pr) return res.status(404).json({ error: 'not found' });
  if (pr.status === 'merged') return res.status(400).json({ error: 'already merged' });
  if (pr.status === 'rejected') return res.status(400).json({ error: 'PR was rejected' });
  if (pr.status === 'closed') return res.status(400).json({ error: 'PR is closed' });

  // Absorb any currently-active agents who joined after creation
  for (const id of activeApproverIds(pr.authorId)) {
    if (!pr.required.includes(id)) pr.required.push(id);
  }

  // Rejection check
  const rejection = pr.votes.find(v => v.decision === 'reject' && pr.required.includes(v.agentId));
  if (rejection) {
    const ag = agentById(rejection.agentId);
    return res.status(400).json({ error: `rejected by ${ag ? ag.name : rejection.agentId}` });
  }
  // Active request-changes check
  for (const v of pr.votes) {
    if (v.decision === 'request_changes' && pr.required.includes(v.agentId)) {
      const ag = agentById(v.agentId);
      if (ag && Date.now() - ag.lastPing < ACTIVE_MS) {
        return res.status(400).json({ error: `changes requested by ${ag.name}: ${v.comment || ''}` });
      }
    }
  }
  // All required must have approved
  const approvedIds = new Set(pr.votes.filter(v => v.decision === 'approve').map(v => v.agentId));
  const missing = pr.required.filter(id => !approvedIds.has(id));
  if (missing.length > 0) {
    return res.status(400).json({ error: `missing approvals (${missing.length})`, missing });
  }

  // Conflict check: verify current disk state matches baseContent
  for (const f of pr.files) {
    try {
      const { abs } = safeRepoPath(f.path);
      let current = '';
      try { current = fs.readFileSync(abs, 'utf8'); } catch { current = ''; }
      if (current !== (f.baseContent || '')) {
        return res.status(409).json({
          error: `الملف ${f.path} تم تعديله خارج الـ PR — أعد فتحه وحدّث الاقتراح`,
          conflict: f.path,
        });
      }
    } catch (e) {
      return res.status(500).json({ error: e.message });
    }
  }

  // Apply
  try {
    await applyFilesToRepo(pr.files);
  } catch (e) {
    return res.status(500).json({ error: 'apply failed: ' + e.message });
  }

  // Commit + push
  const commitMsg = `PR#${pr.id.slice(0, 6)}: ${pr.title}`.slice(0, 200);
  const pushResult = await commitAndPush(commitMsg);
  const shaMatch = pushResult.out.match(/\[[\w./-]+ ([a-f0-9]{7,})\]/);

  pr.status = 'merged';
  pr.mergedAt = Date.now();
  pr.mergedBy = req.body.agentId || null;
  pr.mergedCommit = shaMatch ? shaMatch[1] : null;
  pr.mergeResult = { ok: pushResult.ok, code: pushResult.code, err: pushResult.err };

  recordCommit({
    path: pr.files.map(f => f.path).join(', '),
    message: commitMsg,
    by: pr.authorName,
    sha: pr.mergedCommit || 'local',
    ok: pushResult.ok,
  });
  bcast('pr:updated', prSummary(pr));
  save();
  log(`PR ${pr.id.slice(0, 6)} MERGED by ${req.body.agentId || '?'} (push ok=${pushResult.ok})`);

  res.json({ ok: true, pr: prSummary(pr), push: { ok: pushResult.ok, out: pushResult.out.slice(-2000), err: pushResult.err.slice(-2000) } });
});

app.post('/api/prs/:id/close', auth, (req, res) => {
  const pr = prById(req.params.id);
  if (!pr) return res.status(404).json({ error: 'not found' });
  if (pr.status === 'merged') return res.status(400).json({ error: 'already merged' });
  pr.status = 'closed';
  pr.closedAt = Date.now();
  bcast('pr:updated', prSummary(pr));
  save();
  res.json({ ok: true, pr: prSummary(pr) });
});

// ============================================================================
// ═══════════════════════ GitHub API ═══════════════════════════════════════
// ============================================================================
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
log('=== Agent Ops Center v3 booting ===');
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
  prOpen: (state.prs || []).filter(p => ['open', 'changes_requested', 'approved'].includes(p.status)).length,
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

// ---------------- settings ----------------
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

// ---------------- chat + strict approvals (legacy) ----------------
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

// ---------------- commits feed ----------------
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

// ---------------- GitHub Actions ----------------
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

app.use(express.static(path.join(__dirname, '../public')));
app.get('/', (req, res) => res.sendFile(path.join(__dirname, '../public', 'index.html')));

server.listen(PORT, () => log(`Agent Ops Center v3 listening on :${PORT} (PAT=${!!PAT}, code set=${!!CODE})`));
