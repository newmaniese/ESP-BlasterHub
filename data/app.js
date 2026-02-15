/* ===== ESP32-C3 IR Blaster — Frontend ===== */

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
var ws = null;
var wsUrl = 'ws://' + location.host + '/ws';
var savedIndex = [];   // local cache of saved commands from /saved
var LOG_MAX = 50;

// ---------------------------------------------------------------------------
// Utility helpers
// ---------------------------------------------------------------------------
function esc(s) {
  return (s == null ? '' : String(s))
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;');
}

function ts() {
  var d = new Date();
  return ('0' + d.getHours()).slice(-2) + ':' +
         ('0' + d.getMinutes()).slice(-2) + ':' +
         ('0' + d.getSeconds()).slice(-2);
}

// ---------------------------------------------------------------------------
// Modal
// ---------------------------------------------------------------------------
function showModal(msg) {
  var o = document.getElementById('modal-overlay');
  document.getElementById('modal-msg').textContent = msg;
  o.classList.add('show');
  setTimeout(function () { o.classList.remove('show'); }, 1800);
}

// ---------------------------------------------------------------------------
// Send Log
// ---------------------------------------------------------------------------
function addLog(text, cls) {
  var log = document.getElementById('send-log');
  // Remove the "No activity" placeholder
  var empty = log.querySelector('.log-empty');
  if (empty) empty.remove();

  var div = document.createElement('div');
  div.className = 'log-entry ' + (cls || 'log-unknown');
  div.innerHTML = '<span class="log-ts">' + ts() + '</span> ' + esc(text);
  log.prepend(div);

  // Cap entries
  while (log.children.length > LOG_MAX) {
    log.removeChild(log.lastChild);
  }
}

// ---------------------------------------------------------------------------
// Saved-command matching
// ---------------------------------------------------------------------------
function matchSaved(protocol, value, bits) {
  var proto = (protocol || '').toUpperCase();
  var val = (value || '').toUpperCase();
  var b = parseInt(bits, 10) || 0;
  var exact = null, likely = null;

  for (var i = 0; i < savedIndex.length; i++) {
    var s = savedIndex[i];
    var sp = (s.protocol || '').toUpperCase();
    var sv = (s.value || '').toUpperCase();
    var sb = parseInt(s.bits, 10) || 0;
    if (sp === proto && sv === val && sb === b) { exact = s; break; }
    if (sp === proto && sv === val && !likely) { likely = s; }
  }
  return exact ? { match: 'exact', item: exact }
       : likely ? { match: 'likely', item: likely }
       : { match: 'unknown', item: null };
}

function matchClass(m) {
  if (m.match === 'exact') return 'log-known-exact';
  if (m.match === 'likely') return 'log-known-likely';
  return 'log-unknown';
}

function matchLabel(m, human) {
  if (m.match === 'exact') return '✓ ' + (m.item.name || 'Code ' + m.item.index);
  if (m.match === 'likely') return '≈ ' + (m.item.name || 'Code ' + m.item.index) + ' (bits differ)';
  return human || 'Unknown signal';
}

// ---------------------------------------------------------------------------
// Render stored-commands list
// ---------------------------------------------------------------------------
function renderSavedList(items) {
  savedIndex = items || [];
  var el = document.getElementById('saved-list');
  var h = '';
  if (!items || !items.length) {
    h = '<p id="saved-empty"><em>None yet. Save codes from the form or from received signals.</em></p>';
  } else {
    items.forEach(function (it) {
      var idx = it.index;
      var name = (it.name && it.name.length) ? it.name : ('Code ' + idx);
      var protocol = it.protocol || 'UNKNOWN';
      var value = it.value || '0';
      var bits = it.bits || 32;
      var sendUrl = (protocol.toUpperCase() === 'NEC')
        ? ('/send?type=nec&data=' + encodeURIComponent(value) + '&length=' + bits)
        : '';

      h += '<div class="saved-item" data-index="' + idx + '" data-protocol="' + esc(protocol) + '" data-value="' + esc(value) + '" data-bits="' + bits + '">';
      h += '<span class="saved-name">' + esc(name) + '</span>';
      h += sendUrl
        ? ' <a href="' + sendUrl + '" class="btn btn-send" title="Send">Send</a>'
        : ' <span class="saved-na">(NEC only)</span>';
      h += ' <a href="#" class="btn btn-rename" data-index="' + idx + '" title="Rename">Edit</a>';
      h += ' <a href="#" class="btn btn-delete" data-index="' + idx + '" title="Delete">Del</a>';
      h += '<span class="saved-meta">' + esc(protocol) + ' 0x' + esc(value) + ' ' + bits + 'b</span>';
      h += '</div>';
    });
  }
  el.innerHTML = h;
  document.getElementById('saved-count-n').textContent = (items && items.length) ? items.length : 0;
}

function refreshStoredList() {
  return fetch('/saved').then(function (r) { return r.json(); }).then(function (items) {
    renderSavedList(items);
  });
}

function importSavedFromFile(file) {
  if (!file) return Promise.reject(new Error('Select a JSON file first.'));
  return file.text()
    .then(function (txt) {
      var parsed = JSON.parse(txt);
      if (!Array.isArray(parsed)) {
        throw new Error('JSON must be an array of stored-code objects.');
      }
      return fetch('/saved/import', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(parsed)
      });
    })
    .then(function (r) { return r.json(); });
}

// ---------------------------------------------------------------------------
// Update "Last received" section
// ---------------------------------------------------------------------------
function setLast(data) {
  document.getElementById('last-human').textContent = data.human || '';
  document.getElementById('last-raw').textContent = data.raw || '';

  var a = document.getElementById('last-actions');
  var r = data.replayUrl || '';
  var html = '';
  if (r) {
    html += '<a href="' + esc(r) + '" class="btn btn-send" id="last-replay">Replay</a> ';
  }
  html += '<form id="form-save-last" class="js-save-form" action="/save" method="get" style="display:inline-flex;gap:0.35rem;align-items:center;">';
  html += '<input type="text" name="name" placeholder="Name" style="padding:0.3rem 0.4rem;border:1px solid #ccc;border-radius:6px;font-size:0.9rem;width:8rem;">';
  html += ' <button type="submit" class="btn btn-save-last">Save</button></form>';
  a.innerHTML = html;

  var dot = document.getElementById('last-live');
  dot.classList.add('active');

  // Log the received IR event with matching
  if (data.protocol || data.human) {
    var m = matchSaved(data.protocol, data.value, data.bits);
    var label = 'RX: ' + matchLabel(m, data.human);
    addLog(label, matchClass(m));
  }
}

// ---------------------------------------------------------------------------
// WebSocket
// ---------------------------------------------------------------------------
function setWsStatus(connected) {
  var badge = document.getElementById('ws-status');
  if (connected) {
    badge.textContent = 'Live';
    badge.className = 'ws-badge ws-connected';
  } else {
    badge.textContent = 'Disconnected';
    badge.className = 'ws-badge ws-disconnected';
  }
}

function connectWs() {
  try {
    ws = new WebSocket(wsUrl);
    ws.onopen = function () {
      setWsStatus(true);
      document.getElementById('last-live').classList.add('active');
    };
    ws.onclose = function () {
      setWsStatus(false);
      document.getElementById('last-live').classList.remove('active');
      setTimeout(connectWs, 2000);
    };
    ws.onmessage = function (ev) {
      try {
        var d = JSON.parse(ev.data);
        if (d.event === 'ir') {
          setLast({
            human: d.human,
            raw: d.raw,
            replayUrl: d.replayUrl,
            protocol: d.protocol,
            value: d.value,
            bits: d.bits
          });
        } else if (d.ok && (d.msg || d.name)) {
          showModal(d.name || d.msg);
          addLog('TX ack: ' + (d.name || d.msg), 'log-send');
          // Reset any loading buttons
          document.querySelectorAll('.btn-send').forEach(function (b) {
            if (b.textContent === '…') b.textContent = 'Send';
          });
        }
      } catch (e) { /* ignore parse errors */ }
    };
  } catch (e) {
    setWsStatus(false);
    setTimeout(connectWs, 3000);
  }
}

// ---------------------------------------------------------------------------
// Event delegation: clicks
// ---------------------------------------------------------------------------
document.addEventListener('click', function (e) {
  var t = e.target.closest('a,button');
  if (!t) return;

  // ---- Send ----
  if (t.classList.contains('btn-send') && t.getAttribute('href')) {
    e.preventDefault();
    var u = t.getAttribute('href');
    var row = t.closest('.saved-item');
    var name = (row && row.querySelector('.saved-name'))
      ? row.querySelector('.saved-name').textContent
      : 'Command';
    if (!u) return;
    t.textContent = '…';
    addLog('TX: Sending ' + name + '…', 'log-send');

    if (ws && ws.readyState === WebSocket.OPEN) {
      var m = u.match(/\/send\?type=nec&data=([0-9A-Fa-f]+)&length=(\d+)/);
      if (m) {
        ws.send(JSON.stringify({
          cmd: 'send', type: 'nec', data: m[1],
          length: parseInt(m[2], 10), name: name
        }));
        return;
      }
    }
    // HTTP fallback
    fetch(u).then(function (r) { return r.text(); }).then(function () {
      t.textContent = 'Send';
      showModal(name);
      addLog('TX done (HTTP): ' + name, 'log-send');
    }).catch(function () {
      t.textContent = 'Send';
      addLog('TX failed: ' + name, 'log-failed');
    });
    return;
  }

  // ---- Delete ----
  if (t.classList.contains('btn-delete')) {
    e.preventDefault();
    var idx = t.getAttribute('data-index');
    if (!confirm('Remove this stored code?')) return;
    fetch('/saved/delete?index=' + idx, { method: 'POST' })
      .then(function (r) { return r.json(); })
      .then(function (d) {
        if (d.ok) {
          addLog('Deleted stored code #' + idx, 'log-unknown');
          refreshStoredList();
        }
      });
    return;
  }

  // ---- Rename ----
  if (t.classList.contains('btn-rename')) {
    e.preventDefault();
    var idx2 = t.getAttribute('data-index');
    var row2 = t.closest('.saved-item');
    var cur = (row2 && row2.querySelector('.saved-name'))
      ? row2.querySelector('.saved-name').textContent : '';
    var next = prompt('Rename saved code:', cur);
    if (next === null) return;
    fetch('/saved/rename?index=' + idx2 + '&name=' + encodeURIComponent(next), { method: 'POST' })
      .then(function (r) { return r.json(); })
      .then(function (d) {
        if (d.ok) refreshStoredList();
      });
    return;
  }

  // ---- Save from previous ----
  if (t.classList.contains('btn-save-prev')) {
    e.preventDefault();
    var li = t.closest('li');
    if (!li) return;
    var protocol = li.getAttribute('data-protocol') || '';
    var value = li.getAttribute('data-value') || '';
    var length = li.getAttribute('data-length') || '32';
    var nameInput = li.querySelector('.save-name-prev');
    var pname = nameInput ? nameInput.value.trim() : '';
    var url = '/save?protocol=' + encodeURIComponent(protocol)
            + '&value=' + encodeURIComponent(value)
            + '&length=' + encodeURIComponent(length)
            + '&name=' + encodeURIComponent(pname);
    fetch(url).then(function (r) { return r.json(); }).then(function (d) {
      if (d.ok) {
        showModal('Saved as ' + (pname || 'unnamed'));
        addLog('Saved: ' + (pname || 'unnamed'), 'log-send');
        refreshStoredList();
        if (nameInput) nameInput.value = '';
      }
    });
    return;
  }
});

// ---------------------------------------------------------------------------
// Event delegation: form submissions
// ---------------------------------------------------------------------------
document.addEventListener('submit', function (e) {
  if (e.target.id === 'form-import-saved') {
    e.preventDefault();
    var input = document.getElementById('import-json-file');
    var file = input && input.files ? input.files[0] : null;
    importSavedFromFile(file)
      .then(function (res) {
        if (!res || !res.ok) {
          throw new Error((res && res.error) ? res.error : 'Import failed.');
        }
        var msg = 'Imported ' + (res.imported || 0) + ', skipped ' + (res.skipped || 0);
        addLog('Import: ' + msg, (res.skipped > 0) ? 'log-known-likely' : 'log-send');
        showModal(msg);
        refreshStoredList();
        if (input) input.value = '';
      })
      .catch(function (err) {
        addLog('Import failed: ' + (err && err.message ? err.message : 'unknown error'), 'log-failed');
      });
    return;
  }

  if (!e.target.classList.contains('js-save-form')) return;
  e.preventDefault();
  var f = e.target;
  var p = new URLSearchParams(new FormData(f));
  var name = ((p.get('name') || '') + '').trim();
  fetch('/save?' + p.toString())
    .then(function (r) { return r.json(); })
    .then(function (d) {
      if (d.ok) {
        showModal('Saved as ' + (name || 'unnamed'));
        addLog('Saved: ' + (name || 'unnamed'), 'log-send');
        refreshStoredList();
        f.reset();
      }
    });
});

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------
refreshStoredList();
connectWs();
