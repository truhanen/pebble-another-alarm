/* Clay custom component "vibeList": a variable-length list (max 20) of named
   custom vibration patterns (name + pattern text, e.g. "2 z 2"). Value is an
   array of { id, name, pattern }. The vibration counterpart of tuneList
   (config_tune_list.ts): alarms reference these by id. Clay serializes this
   whole object via toSource() and re-evals it in the config webview, so every
   function must be self-contained: no module-scope references at runtime, no
   spread/destructuring, native DOM only. The pattern check below deliberately
   duplicates list_config.ts's validVibePattern (the authoritative, unit-tested
   copy that also gates what Save sends; tests/pattern_parity.test.js checks it
   against the watch's real parser). Never write a dollar sign next to a quote/
   backtick/ampersand here, even in a comment: Clay injects this source with
   String.replace, which reads those as replacement patterns.
   The page also shows the watch's built-in presets read-only (config.builtins) and a
   collapsible long syntax help (config.help), both supplied by config_clay.ts. */

function vibeListInitialize(this: any, _minified: any, _clayConfig: any): void {
  const self = this;
  const root: HTMLElement = self.$element[0];
  const MAX = 20;   // MAX_CUSTOM_VIBES

  // Phone-owned ids are >= 0x10000; the watch allocates its own far below.
  function genId(): number {
    return 0x10000 + Math.floor(Math.random() * 0x3fff0000);
  }

  function esc(s: string): string {
    return ('' + s).replace(/&/g, '&amp;').replace(/"/g, '&quot;')
      .replace(/</g, '&lt;').replace(/>/g, '&gt;');
  }

  function validPattern(text: string): boolean {
    // Space-separated steps, at most 8: an on-beat is its duration ratio
    // (2, 1/2, /2), z[ratio] is a pause. The watch splits on spaces only.
    const toks = ('' + text).replace(/^ +| +$/g, '').split(/ +/);
    if (toks.length === 0 || toks[0] === '' || toks.length > 8) { return false; }
    const on = /^(?:[1-8](?:\/[1-8])?|\/[1-8])$/;
    const rest = /^z(?:[1-8](?:\/[1-8])?|\/[1-8])?$/;
    for (let i = 0; i < toks.length; i++) {
      if (!on.test(toks[i]) && !rest.test(toks[i])) { return false; }
    }
    return true;
  }

  function rowHtml(id: number, name: string, pattern: string): string {
    return '<div class="vb-row" data-id="' + id + '">' +
      '<div class="vb-line">' +
      '<input class="vb-name" type="text" maxlength="31" placeholder="Name" value="' + esc(name) + '">' +
      '<button type="button" class="vb-del" title="Remove">&#10005;</button>' +
      '</div>' +
      '<input class="vb-pat" type="text" placeholder="Pattern, e.g. 2 z 2" value="' + esc(pattern) + '">' +
      '<div class="vb-err"></div>' +
      '</div>';
  }

  function refreshValidity(row: HTMLElement): void {
    const pat = (row.querySelector('.vb-pat') as HTMLInputElement).value;
    const err = row.querySelector('.vb-err') as HTMLElement;
    err.textContent = (pat.trim() === '' || validPattern(pat)) ? '' : 'Invalid pattern (will not be saved)';
  }

  function currentValue(): any[] {
    const out: any[] = [];
    const rows = root.querySelectorAll('.vb-row');
    for (let i = 0; i < rows.length; i++) {
      const r = rows[i] as HTMLElement;
      out.push({
        id: parseInt(r.getAttribute('data-id') || '', 10) || genId(),
        name: (r.querySelector('.vb-name') as HTMLInputElement).value,
        pattern: (r.querySelector('.vb-pat') as HTMLInputElement).value,
      });
    }
    return out;
  }

  function updateAdd(): void {
    const add = root.querySelector('.vb-add') as HTMLButtonElement;
    if (add) { add.style.display = (root.querySelectorAll('.vb-row').length >= MAX) ? 'none' : ''; }
  }

  function rebuild(list: any[]): void {
    const listEl = root.querySelector('.vb-list') as HTMLElement;
    let html = '';
    for (let i = 0; i < list.length && i < MAX; i++) {
      const e = list[i] || {};
      html += rowHtml(parseInt(e.id, 10) || genId(),
        typeof e.name === 'string' ? e.name : '',
        typeof e.pattern === 'string' ? e.pattern : '');
    }
    listEl.innerHTML = html;
    const rows = root.querySelectorAll('.vb-row');
    for (let i = 0; i < rows.length; i++) { refreshValidity(rows[i] as HTMLElement); }
    updateAdd();
  }

  self._vbCurrentValue = currentValue;
  self._vbRebuild = rebuild;

  // ---- read-only built-ins + collapsible long help (from the item's config) ----
  const cfg: any = self.config || {};
  const builtins: any[] = Array.isArray(cfg.builtins) ? cfg.builtins : [];
  let biHtml = '';
  for (let i = 0; i < builtins.length; i++) {
    biHtml += '<div class="vb-bi"><span class="vb-biname">' + esc(builtins[i].name) + '</span>' +
      '<input class="vb-bipat" type="text" readonly value="' + esc(builtins[i].pattern) + '"></div>';
  }
  (root.querySelector('.vb-builtins') as HTMLElement).innerHTML = biHtml;
  const helpParas = ('' + (cfg.help || '')).split(/\n\n+/);
  let helpHtml = '';
  for (let i = 0; i < helpParas.length; i++) {
    if (helpParas[i].replace(/^ +| +$/g, '') !== '') { helpHtml += '<p>' + esc(helpParas[i]) + '</p>'; }
  }
  (root.querySelector('.vb-helptext') as HTMLElement).innerHTML = helpHtml;
  // Tapping a read-only value selects all of it, so it can be copied natively.
  root.addEventListener('focusin', function(ev: Event) {
    const t = ev.target as HTMLInputElement;
    if (t && t.classList && t.hasAttribute && t.hasAttribute('readonly')) {
      t.select();
      try { t.setSelectionRange(0, 9999); } catch (e) { /* ignore */ }
    }
  });
  root.addEventListener('input', function(ev: Event) {
    const t = ev.target as HTMLElement;
    const row = t && t.closest ? (t.closest('.vb-row') as HTMLElement) : null;
    if (row) { refreshValidity(row); }
  });

  root.addEventListener('click', function(ev: Event) {
    let target = ev.target as HTMLElement;
    if (!target) { return; }
    if (target.tagName !== 'BUTTON') {
      target = target.closest ? (target.closest('button') as HTMLElement) : null as any;
    }
    if (!target) { return; }
    if (target.classList.contains('vb-add')) {
      const v = currentValue();
      if (v.length < MAX) { v.push({ id: genId(), name: '', pattern: '' }); rebuild(v); self.trigger('change'); }
      return;
    }
    if (target.classList.contains('vb-del')) {
      const row = (target.parentNode as HTMLElement).parentNode as HTMLElement;   // button > .vb-line > .vb-row
      const rows = root.querySelectorAll('.vb-row');
      let idx = -1;
      for (let i = 0; i < rows.length; i++) { if (rows[i] === row) { idx = i; } }
      if (idx === -1) { return; }
      const v = currentValue();
      v.splice(idx, 1);
      rebuild(v);
      self.trigger('change');
    }
  });
}

const vibeListComponent = {
  name: 'vibeList',
  template:
    '<div class="vb-rootc">' +
    '<details class="vb-helpbox"><summary>Syntax help</summary><div class="vb-helptext"></div></details>' +
    '<div class="vb-sub">Built-in patterns (read-only)</div>' +
    '<div class="vb-builtins"></div>' +
    '<div class="vb-sub">Your vibration patterns</div>' +
    '<div class="vb-list"></div>' +
    '<button type="button" class="vb-add">+ Add vibration</button>' +
    '</div>',
  style:
    // Same inset as Clay's own components (12px at the sides and below the heading),
    // so nothing touches the section outline.
    '.vb-rootc{padding:0.75rem 0.75rem 0.5rem 0.75rem;box-sizing:border-box}' +
    '.vb-row{margin:0 0 12px 0}' +
    '.vb-line{display:flex;align-items:center;margin:0 0 6px 0}' +
    '.vb-name,.vb-pat{box-sizing:border-box;height:2.8rem;margin:0;background-color:#767676;color:#fff;' +
      'border:none;border-radius:0.3rem;padding:0 0.5rem;color-scheme:dark}' +
    '.vb-name{flex:1 1 auto;min-width:0}' +
    '.vb-pat{width:100%;font-family:monospace}' +
    '.vb-line button{flex:0 0 auto;min-width:0;width:2.8rem;height:2.8rem;margin:0 0 0 6px;padding:0}' +
    '.vb-err{color:#ff6b6b;font-size:0.8rem;min-height:0}' +
    '.vb-add{margin:8px 0 6px 0}' +
    // collapsible long syntax help
    '.vb-helpbox{margin:0 0 0.8rem 0;background:#444;border:1px solid #555;border-radius:0.5rem}' +
    '.vb-helpbox summary{cursor:pointer;padding:0.6rem 0.8rem;color:#ddd;font-size:0.95rem}' +
    '.vb-helptext{padding:0 0.8rem 0.6rem 0.8rem;color:#ccc;font-size:0.85rem;line-height:1.4}' +
    '.vb-helptext p{margin:0 0 0.6rem 0;white-space:pre-wrap}' +
    '.vb-helptext code{font-family:monospace;color:#fff}' +
    '.vb-sub{color:#bbb;font-size:0.8rem;text-transform:uppercase;letter-spacing:0.04em;margin:0.8rem 0 0.4rem 0}' +
    // built-in (read-only) rows: same shape as editable ones, dimmer, and
    // selectable so the value can be copied
    // one line each: plain-text label + read-only, selectable value field
    '.vb-bi{display:flex;align-items:center;margin:0 0 6px 0}' +
    '.vb-biname{flex:0 0 6.6rem;min-width:0;color:#ddd;font-size:0.95rem;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;padding-right:0.5rem}' +
    '.vb-bipat{flex:1 1 auto;min-width:0;box-sizing:border-box;height:2.3rem;margin:0;background-color:#555;color:#ddd;border:none;' +
      'border-radius:0.3rem;padding:0 0.5rem;color-scheme:dark;font-family:monospace;-webkit-user-select:all;user-select:all}',
  manipulator: {
    get: function(this: any): any[] {
      return this._vbCurrentValue ? this._vbCurrentValue() : [];
    },
    set: function(this: any, value: any) {
      let list: any[] = [];
      let v: any = value;
      if (v && typeof v === 'object' && !Array.isArray(v) && v.value !== undefined) { v = v.value; }
      if (Array.isArray(v)) {
        list = v;
      } else if (typeof v === 'string' && v !== '') {
        try { const p = JSON.parse(v); if (Array.isArray(p)) { list = p; } } catch (e) { list = []; }
      }
      if (this._vbRebuild) { this._vbRebuild(list); }
      return this;
    },
  },
  defaults: { label: '' },
  initialize: vibeListInitialize,
};

export = vibeListComponent;
