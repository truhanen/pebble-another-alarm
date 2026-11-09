/* Clay custom component "tuneList": a variable-length list (max 20) of named
   ABC-notation tunes (name + pattern text, e.g. "C z E z G"). Value is an array
   of { id, name, pattern }. Modeled on pebble-another-timer's timerList.
   Clay serializes this whole object via toSource() and re-evals it in the
   config webview, so every function must be self-contained: no module-scope
   references at runtime, no spread/destructuring, native DOM only. The pattern
   check below deliberately duplicates list_config.ts's validTunePattern (the
   authoritative, unit-tested copy, which also gates what Save actually sends). */

function tuneListInitialize(this: any, _minified: any, _clayConfig: any): void {
  const self = this;
  const root: HTMLElement = self.$element[0];
  const MAX = 20;   // MAX_CUSTOM_TUNES

  // Phone-owned ids are >= 0x10000; the watch allocates its own far below.
  function genId(): number {
    return 0x10000 + Math.floor(Math.random() * 0x3fff0000);
  }

  function esc(s: string): string {
    return ('' + s).replace(/&/g, '&amp;').replace(/"/g, '&quot;')
      .replace(/</g, '&lt;').replace(/>/g, '&gt;');
  }

  function validPattern(text: string): boolean {
    const toks = ('' + text).trim().split(/\s+/);
    if (toks.length === 0 || toks[0] === '' || toks.length > 32) { return false; }
    // Regex LITERALS on purpose. Clay injects this source with
    // String.replace(), so a dollar sign followed by a quote anywhere in this
    // function (even in a comment) is read as a special replacement pattern
    // and corrupts the generated page.
    const note = /^[\^_]?[C-GAB](?:'|,{1,8})?(?:[1-8](?:\/[1-8])?|\/[1-8])?$/;
    const rest = /^z(?:[1-8](?:\/[1-8])?|\/[1-8])?$/;
    for (let i = 0; i < toks.length; i++) {
      if (!note.test(toks[i]) && !rest.test(toks[i])) { return false; }
    }
    return true;
  }

  function rowHtml(id: number, name: string, pattern: string): string {
    return '<div class="tu-row" data-id="' + id + '">' +
      '<div class="tu-line">' +
      '<input class="tu-name" type="text" maxlength="31" placeholder="Name" value="' + esc(name) + '">' +
      '<button type="button" class="tu-del" title="Remove">&#10005;</button>' +
      '</div>' +
      '<input class="tu-pat" type="text" placeholder="Pattern, e.g. C z E z G" value="' + esc(pattern) + '">' +
      '<div class="tu-err"></div>' +
      '</div>';
  }

  function refreshValidity(row: HTMLElement): void {
    const pat = (row.querySelector('.tu-pat') as HTMLInputElement).value;
    const err = row.querySelector('.tu-err') as HTMLElement;
    err.textContent = (pat.trim() === '' || validPattern(pat)) ? '' : 'Invalid pattern (will not be saved)';
  }

  function currentValue(): any[] {
    const out: any[] = [];
    const rows = root.querySelectorAll('.tu-row');
    for (let i = 0; i < rows.length; i++) {
      const r = rows[i] as HTMLElement;
      out.push({
        id: parseInt(r.getAttribute('data-id') || '', 10) || genId(),
        name: (r.querySelector('.tu-name') as HTMLInputElement).value,
        pattern: (r.querySelector('.tu-pat') as HTMLInputElement).value,
      });
    }
    return out;
  }

  function updateAdd(): void {
    const add = root.querySelector('.tu-add') as HTMLButtonElement;
    if (add) { add.style.display = (root.querySelectorAll('.tu-row').length >= MAX) ? 'none' : ''; }
  }

  function rebuild(list: any[]): void {
    const listEl = root.querySelector('.tu-list') as HTMLElement;
    let html = '';
    for (let i = 0; i < list.length && i < MAX; i++) {
      const e = list[i] || {};
      html += rowHtml(parseInt(e.id, 10) || genId(),
        typeof e.name === 'string' ? e.name : '',
        typeof e.pattern === 'string' ? e.pattern : '');
    }
    listEl.innerHTML = html;
    const rows = root.querySelectorAll('.tu-row');
    for (let i = 0; i < rows.length; i++) { refreshValidity(rows[i] as HTMLElement); }
    updateAdd();
  }

  self._tuCurrentValue = currentValue;
  self._tuRebuild = rebuild;

  root.addEventListener('input', function(ev: Event) {
    const t = ev.target as HTMLElement;
    const row = t && t.closest ? (t.closest('.tu-row') as HTMLElement) : null;
    if (row) { refreshValidity(row); }
  });

  root.addEventListener('click', function(ev: Event) {
    let target = ev.target as HTMLElement;
    if (!target) { return; }
    if (target.tagName !== 'BUTTON') {
      target = target.closest ? (target.closest('button') as HTMLElement) : null as any;
    }
    if (!target) { return; }
    if (target.classList.contains('tu-add')) {
      const v = currentValue();
      if (v.length < MAX) { v.push({ id: genId(), name: '', pattern: '' }); rebuild(v); self.trigger('change'); }
      return;
    }
    if (target.classList.contains('tu-del')) {
      const row = (target.parentNode as HTMLElement).parentNode as HTMLElement;   // button > .tu-line > .tu-row
      const rows = root.querySelectorAll('.tu-row');
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

const tuneListComponent = {
  name: 'tuneList',
  template:
    '<div class="tu-rootc">' +
    '<div class="tu-list"></div>' +
    '<button type="button" class="tu-add">+ Add tune</button>' +
    '<div class="tu-help">Notes C D E F G A B, optional ^ (sharp) / _ (flat) before, \' or , after for octave, ' +
    'then a length like 2 or 1/2 (default 1 = 100 ms). z = rest. Example: C z E2 G/2</div>' +
    '</div>',
  style:
    '.tu-row{margin:0 0 12px 0}' +
    '.tu-line{display:flex;align-items:center;margin:0 0 6px 0}' +
    '.tu-name,.tu-pat{box-sizing:border-box;height:2.8rem;margin:0;background-color:#767676;color:#fff;' +
      'border:none;border-radius:0.3rem;padding:0 0.5rem;color-scheme:dark}' +
    '.tu-name{flex:1 1 auto;min-width:0}' +
    '.tu-pat{width:100%;font-family:monospace}' +
    '.tu-line button{flex:0 0 auto;min-width:0;width:2.8rem;height:2.8rem;margin:0 0 0 6px;padding:0}' +
    '.tu-err{color:#ff6b6b;font-size:0.8rem;min-height:0}' +
    '.tu-add{margin:8px 0 6px 0}' +
    '.tu-help{font-size:0.8rem;color:#bbb;margin:4px 0 8px 0}',
  manipulator: {
    get: function(this: any): any[] {
      return this._tuCurrentValue ? this._tuCurrentValue() : [];
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
      if (this._tuRebuild) { this._tuRebuild(list); }
      return this;
    },
  },
  defaults: { label: '' },
  initialize: tuneListInitialize,
};

export = tuneListComponent;
