#pragma once

#include <Arduino.h>

// Web control portal served at "/". Kept in PROGMEM (flash) and sent with
// server.send_P() so it never occupies RAM.
static const char PORTAL_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Split-Flap Display</title>
  <style>
    :root {
      --bg: #0f1115;
      --card: #171b22;
      --line: #272d38;
      --text: #e8eaed;
      --muted: #98a1ad;
      --accent: #f5b52e;
      --green: #2ecc71;
      --red: #e5484d;
      --btn: #222834;
    }

    * {
      box-sizing: border-box;
    }

    body {
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto,
        Helvetica, Arial, sans-serif;
      background: var(--bg);
      color: var(--text);
      margin: 0 auto;
      padding: 20px 16px 72px;
      max-width: 540px;
    }

    header {
      display: flex;
      align-items: center;
      justify-content: space-between;
      margin: 0 0 14px;
    }

    h1 {
      font-size: 19px;
      font-weight: 650;
      margin: 0;
    }

    .pill {
      display: inline-flex;
      align-items: center;
      gap: 7px;
      font-size: 12px;
      color: var(--muted);
      background: var(--card);
      border: 1px solid var(--line);
      padding: 5px 11px;
      border-radius: 999px;
    }

    .dot {
      width: 8px;
      height: 8px;
      border-radius: 50%;
      background: var(--red);
    }

    .dot.on {
      background: var(--green);
    }

    .board {
      background: #000;
      border: 1px solid var(--line);
      border-radius: 12px;
      padding: 18px 10px;
      text-align: center;
      box-shadow: inset 0 0 28px rgba(245, 181, 46, 0.05);
    }

    .board div {
      font-family: Consolas, "Courier New", monospace;
      font-size: clamp(13px, 4.2vw, 20px);
      letter-spacing: 0.32em;
      white-space: pre;
      color: var(--accent);
      text-shadow: 0 0 9px rgba(245, 181, 46, 0.3);
      line-height: 1.7;
      overflow: hidden;
    }

    h2 {
      font-size: 13px;
      font-weight: 600;
      color: var(--muted);
      margin: 0;
      text-transform: uppercase;
      letter-spacing: 0.08em;
    }

    .seg {
      display: flex;
      background: var(--card);
      border: 1px solid var(--line);
      border-radius: 10px;
      padding: 4px;
      gap: 4px;
      margin: 18px 0 22px;
    }

    .seg button {
      flex: 1;
      padding: 11px 0;
      border: 0;
      border-radius: 8px;
      background: transparent;
      color: var(--muted);
      font-size: 14px;
      font-weight: 600;
      cursor: pointer;
      transition: background 0.15s, color 0.15s;
    }

    .seg button.active {
      background: var(--accent);
      color: #141414;
    }

    .card {
      background: var(--card);
      border: 1px solid var(--line);
      border-radius: 12px;
      padding: 16px;
      margin: 0 0 16px;
    }

    .card-h {
      display: flex;
      align-items: center;
      justify-content: space-between;
      margin-bottom: 8px;
    }

    .badge {
      font-size: 11px;
      font-weight: 600;
      padding: 3px 9px;
      border-radius: 999px;
      background: rgba(229, 72, 77, 0.12);
      color: var(--red);
    }

    .badge.ok {
      background: rgba(46, 204, 113, 0.12);
      color: var(--green);
    }

    label {
      display: block;
      font-size: 12px;
      color: var(--muted);
      margin: 10px 0 5px;
    }

    input {
      width: 100%;
      padding: 10px 11px;
      border-radius: 8px;
      border: 1px solid var(--line);
      background: #10141a;
      color: var(--text);
      font-size: 14px;
      outline: none;
      transition: border-color 0.15s;
    }

    input:focus {
      border-color: var(--accent);
    }

    .grid {
      display: grid;
      grid-template-columns: 1fr 1fr 1fr;
      gap: 10px;
    }

    .grid2 {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 10px;
    }

    .hint {
      font-size: 11.5px;
      color: var(--muted);
      margin: 6px 0 0;
    }

    .hint a {
      color: var(--accent);
      text-decoration: none;
    }

    .save {
      width: 100%;
      margin-top: 14px;
      padding: 11px;
      border: 0;
      border-radius: 8px;
      background: var(--btn);
      color: var(--text);
      font-size: 14px;
      font-weight: 600;
      cursor: pointer;
      transition: background 0.15s;
    }

    .save:hover {
      background: #2b3342;
    }

    .toast {
      position: fixed;
      left: 50%;
      bottom: 22px;
      transform: translate(-50%, 80px);
      background: #e8eaed;
      color: #14171c;
      font-size: 14px;
      font-weight: 600;
      padding: 10px 20px;
      border-radius: 999px;
      transition: transform 0.25s;
      pointer-events: none;
    }

    .toast.show {
      transform: translate(-50%, 0);
    }

    .preset {
      display: flex;
      align-items: center;
      gap: 8px;
      background: #10141a;
      border: 1px solid var(--line);
      border-radius: 8px;
      padding: 8px 12px;
      margin-top: 8px;
      cursor: pointer;
      transition: border-color 0.15s;
    }

    .preset:hover {
      border-color: var(--accent);
    }

    .preset .tx {
      flex: 1;
      font-family: Consolas, "Courier New", monospace;
      font-size: 12.5px;
      white-space: pre;
      overflow: hidden;
      text-overflow: ellipsis;
    }

    .preset .del {
      background: none;
      border: 0;
      color: var(--muted);
      font-size: 15px;
      cursor: pointer;
      padding: 2px 6px;
      border-radius: 6px;
    }

    .preset .del:hover {
      color: var(--red);
    }

    .presets-empty {
      font-size: 12px;
      color: var(--muted);
      margin-top: 10px;
    }
  </style>
</head>
<body>
  <header>
    <h1>Split-Flap Display</h1>
    <span class="pill">
      <span class="dot" id="dot"></span>
      <span id="conn">Connecting</span>
    </span>
  </header>

  <div class="board">
    <div id="r1"> </div>
    <div id="r2"> </div>
  </div>

  <div class="seg" id="seg">
    <button data-f="trains">Trains</button>
    <button data-f="clock">Clock</button>
    <button data-f="spotify">Spotify</button>
    <button data-f="text">Text</button>
  </div>

  <section class="card">
    <div class="card-h">
      <h2>Custom Text</h2>
    </div>
    <form id="fText">
      <label>Top row (max 17 chars)</label>
      <input name="row1" maxlength="17" autocomplete="off" placeholder="HELLO">
      <label>Bottom row (max 17 chars)</label>
      <input name="row2" maxlength="17" autocomplete="off" placeholder="WORLD">
      <div class="grid2">
        <button class="save" type="submit">Display on board</button>
        <button class="save" type="button" id="savePreset">Save as preset</button>
      </div>
    </form>
    <div id="presets"></div>
  </section>

  <section class="card">
    <div class="card-h">
      <h2>Train Settings</h2>
      <span class="badge" id="bKey">API key missing</span>
    </div>
    <form id="fTrains">
      <label>WMATA API key</label>
      <input name="apikey" placeholder="Paste new key to update" autocomplete="off">
      <p class="hint">
        Get a free key at
        <a href="https://developer.wmata.com" target="_blank" rel="noopener">developer.wmata.com</a>.
        Leave blank to keep the saved key.
      </p>
      <div class="grid">
        <div>
          <label>Station code</label>
          <input name="station" placeholder="A01">
        </div>
        <div>
          <label>Walk time (min)</label>
          <input name="thresh" type="number" min="0">
        </div>
        <div>
          <label>Refresh (sec)</label>
          <input name="refresh" type="number" min="5">
        </div>
      </div>
      <button class="save">Save train settings</button>
    </form>
  </section>

  <section class="card">
    <div class="card-h">
      <h2>Spotify</h2>
      <span class="badge" id="bSp">Not connected</span>
    </div>
    <form id="fSpotify">
      <label>Client ID</label>
      <input name="id" autocomplete="off">
      <label>Client secret</label>
      <input name="secret" type="password" autocomplete="off">
      <label>Refresh token</label>
      <input name="token" type="password" autocomplete="off">
      <p class="hint">Saved values are kept unless you enter new ones.</p>
      <button class="save">Save Spotify settings</button>
    </form>
  </section>

  <div class="toast" id="toast">Saved</div>

  <script>
    const $ = (id) => document.getElementById(id);

    function toast(message) {
      const element = $('toast');
      element.textContent = message;
      element.classList.add('show');
      clearTimeout(element._h);
      element._h = setTimeout(() => element.classList.remove('show'), 2200);
    }

    document.querySelectorAll('#seg button').forEach((button) => {
      button.onclick = () => {
        fetch('/feature', {
          method: 'POST',
          headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
          body: 'name=' + button.dataset.f,
        })
          .then(() => {
            toast('Switched to ' + button.textContent);
            poll();
          })
          .catch(() => toast('Failed to switch'));
      };
    });

    function wire(formId, url, message) {
      $(formId).onsubmit = (event) => {
        event.preventDefault();
        const form = event.target;
        fetch(url, {
          method: 'POST',
          headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
          body: new URLSearchParams(new FormData(form)),
        })
          .then((response) => {
            if (!response.ok) throw 0;
            toast(message);
            form
              .querySelectorAll('input[type=password], input[name=apikey], input[name=id]')
              .forEach((input) => (input.value = ''));
            loadConfig();
          })
          .catch(() => toast('Save failed'));
      };
    }

    wire('fTrains', '/trains', 'Train settings saved');
    wire('fSpotify', '/spotify', 'Spotify settings saved');

    $('fText').onsubmit = (event) => {
      event.preventDefault();
      const form = event.target;
      fetch('/text', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: new URLSearchParams(new FormData(form)),
      })
        .then((response) => {
          if (!response.ok) throw 0;
          toast('Displaying on board');
          poll();
        })
        .catch(() => toast('Failed'));
    };

    $('savePreset').onclick = () => {
      const form = $('fText');
      if (!form.row1.value && !form.row2.value) {
        toast('Type something first');
        return;
      }
      fetch('/preset', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: new URLSearchParams({
          action: 'save',
          row1: form.row1.value,
          row2: form.row2.value,
        }),
      })
        .then((response) => {
          if (!response.ok) throw 0;
          toast('Preset saved');
          loadPresets();
        })
        .catch(() => toast('Preset slots full (max 6)'));
    };

    function loadPresets() {
      fetch('/presets')
        .then((response) => response.json())
        .then((presets) => {
          const container = $('presets');
          container.innerHTML = '';
          if (!presets.length) {
            container.innerHTML =
              '<p class="presets-empty">No presets yet. Type text above and press "Save as preset".</p>';
            return;
          }
          presets.forEach((preset) => {
            const row = document.createElement('div');
            row.className = 'preset';
            row.title = 'Click to display';

            const text = document.createElement('span');
            text.className = 'tx';
            text.textContent = (preset.r1 || ' ') + '\n' + (preset.r2 || ' ');
            row.onclick = () => {
              const form = $('fText');
              form.row1.value = preset.r1;
              form.row2.value = preset.r2;
              form.requestSubmit();
            };

            const deleteButton = document.createElement('button');
            deleteButton.className = 'del';
            deleteButton.textContent = '\u2715';
            deleteButton.title = 'Delete preset';
            deleteButton.onclick = (event) => {
              event.stopPropagation();
              fetch('/preset', {
                method: 'POST',
                headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                body: new URLSearchParams({
                  action: 'delete',
                  slot: preset.slot,
                }),
              })
                .then(() => {
                  toast('Preset deleted');
                  loadPresets();
                })
                .catch(() => toast('Delete failed'));
            };

            row.append(text, deleteButton);
            container.append(row);
          });
        })
        .catch(() => {});
    }

    function loadConfig() {
      fetch('/config')
        .then((response) => response.json())
        .then((config) => {
          const trainsForm = $('fTrains');
          trainsForm.station.value = config.station;
          trainsForm.thresh.value = config.thresh;
          trainsForm.refresh.value = config.refresh;

          const textForm = $('fText');
          if (document.activeElement !== textForm.row1) {
            textForm.row1.value = config.text1;
          }
          if (document.activeElement !== textForm.row2) {
            textForm.row2.value = config.text2;
          }

          $('bKey').textContent = config.apikey ? 'API key saved' : 'API key missing';
          $('bKey').classList.toggle('ok', config.apikey);
          $('bSp').textContent = config.spotify ? 'Connected' : 'Not connected';
          $('bSp').classList.toggle('ok', config.spotify);
        })
        .catch(() => {});
    }

    function poll() {
      fetch('/status')
        .then((response) => response.json())
        .then((status) => {
          $('r1').textContent = status.row1 || ' ';
          $('r2').textContent = status.row2 || ' ';
          $('dot').classList.add('on');
          $('conn').textContent = 'Online';
          document.querySelectorAll('#seg button').forEach((button) => {
            button.classList.toggle('active', status.feature === button.dataset.f);
          });
        })
        .catch(() => {
          $('dot').classList.remove('on');
          $('conn').textContent = 'Offline';
        });
    }

    setInterval(poll, 3000);
    poll();
    loadConfig();
    loadPresets();
  </script>
</body>
</html>
)HTML";
