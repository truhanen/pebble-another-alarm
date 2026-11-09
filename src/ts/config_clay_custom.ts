// Clay custom function: turns each `heading` item into an accordion header.
// Only one section is open at a time, all collapsed on load (ported from
// pebble-aika-watchface's config_clay_custom.ts, minus its widget gating).
// Clay serializes this via toSource() and re-evals it in the config webview,
// so it must be fully self-contained: no module-scope refs, no spread, no
// for..of, no TS downlevel helpers.

function clayConfigCustom(this: any): void {
  const clayConfig = this;
  let groups: any[] = [];
  let openIndex = -1;

  // Toggle Clay's `hide` class on the item's root element directly rather
  // than it.show()/it.hide(): custom components (alarmList/tuneList) have a
  // plain {get,set} manipulator with no hide/show methods.
  function setShown(it: any, on: boolean): void {
    if (on) { it.$element.set('-hide'); } else { it.$element.set('+hide'); }
  }

  function buildGroups(): void {
    groups = [];
    const items = clayConfig.getAllItems();
    let cur: any = null;
    for (let i = 0; i < items.length; i++) {
      const it = items[i];
      if (it.config.type === 'heading') {
        cur = { heading: it, items: [], label: String(it.config.defaultValue || '') };
        groups.push(cur);
      } else if (cur) {
        cur.items.push(it);
      }
    }
  }

  function applyVisibility(): void {
    for (let gi = 0; gi < groups.length; gi++) {
      const grp = groups[gi];
      const open = gi === openIndex;
      grp.heading.$manipulatorTarget.set('innerHTML', (open ? '▾' : '▸') + ' ' + grp.label);
      for (let j = 0; j < grp.items.length; j++) {
        const it = grp.items[j];
        if (it.config.type === 'submit') { it.$element.set('-hide'); continue; }
        setShown(it, open);
      }
    }
  }

  function wireHeadings(): void {
    groups.forEach(function(grp: any, idx: number) {
      grp.heading.$element.on('click', function() {
        openIndex = (openIndex === idx) ? -1 : idx;
        applyVisibility();
      });
    });
  }

  function injectStyle(id: string, css: string): void {
    if (typeof document === 'undefined' || document.getElementById(id)) { return; }
    const style = document.createElement('style');
    style.id = id;
    style.textContent = css;
    document.head.appendChild(style);
  }

  clayConfig.on(clayConfig.EVENTS.AFTER_BUILD, function() {
    // Save bar pinned to the bottom; clearance goes on #main-form (not body,
    // since Clay sets html,body{height:100%}).
    injectStyle('ac-floating-save',
      '.component-submit{position:fixed;bottom:0;left:0;right:0;margin:0;z-index:100;' +
      'background:#262626;padding:8px 0;box-shadow:0 -2px 6px rgba(0,0,0,0.4);}' +
      '#main-form{padding-bottom:96px;}');
    injectStyle('ac-accordion',
      '.component-heading{cursor:pointer;background:#2b2b2b;margin:0;padding:8px;border-top:1px solid #000;}' +
      '.component-heading h1,.component-heading h2,.component-heading h3,' +
      '.component-heading h4,.component-heading h5{margin:0;}' +
      '.component-heading:active{background:#3a3a3a;}');
    buildGroups();
    openIndex = -1;
    wireHeadings();
    applyVisibility();
  });
}

export = clayConfigCustom;
