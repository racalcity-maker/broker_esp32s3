// NOTE: This bundle is generated from assets/wizard/*.js via build_devices_wizard.py.
//       Edit the source modules rather than the assembled devices_wizard.js.
(() => {
const ACTION_TYPES = ['mqtt_publish','audio_play','audio_stop','set_flag','wait_flags','loop','delay','event','nop'];
const TEMPLATE_TYPES = [
  {value: '', label: 'No template'},
  {value: 'uid_validator', label: 'UID Validator'},
  {value: 'signal_hold', label: 'Signal Hold'},
  {value: 'on_mqtt_event', label: 'When MQTT message arrives'},
  {value: 'on_flag', label: 'When flag changes'},
  {value: 'if_condition', label: 'Conditional branch'},
  {value: 'interval_task', label: 'Run on interval'},
  {value: 'sequence_lock', label: 'Sequence Lock'},
];
const MQTT_RULE_LIMIT = 8;
const FLAG_RULE_LIMIT = 8;
const SEQUENCE_STEP_LIMIT = 8;
const WIZARD_TEMPLATES = {
  blank_device: {
    label: 'Blank Device',
    description: 'Start from a minimal empty device and edit details manually.',
    defaults: {
      deviceName: 'New device',
    },
    fields: [],
    build(base) {
      base.scenarios = [];
      return base;
    },
  },
};

const state = {
  root: null,
  toolbar: null,
  list: null,
  detail: null,
  profileList: null,
  profileActions: null,
  json: null,
  jsonWrap: null,
  statusEl: null,
  validationEl: null,
  trackPickerModal: null,
  trackPickerContent: null,
  model: null,
  profiles: [],
  activeProfile: '',
  actionsRoot: null,
  selectedDevice: -1,
  selectedScenario: -1,
  busy: false,
  dirty: false,
  jsonVisible: false,
  validation: {errors: [], warnings: []},
  trackPicker: {
    open: false,
    query: '',
    targetInput: null,
  },
  wizard: {
    open: false,
    step: 0,
    template: null,
    data: {},
    autoId: true,
  },
  wizardModal: null,
  wizardContent: null,
  userRole: 'admin',
};

let initialized = false;
let waitingForRole = false;

function init() {
  if (initialized) {
    return;
  }
  const currentRole = (window.__WEB_SESSION && window.__WEB_SESSION.role) || '';
  if (!currentRole) {
    const promise = window.__sessionRolePromise;
    if (promise && typeof promise.then === 'function' && !waitingForRole) {
      waitingForRole = true;
      promise.finally(() => {
        waitingForRole = false;
        init();
      });
      return;
    }
  }
  state.userRole = currentRole || state.userRole || 'admin';
  state.root = document.getElementById('device_wizard_root');
  state.actionsRoot = document.getElementById('actions_root');
  if (!state.root) {
    return;
  }
  initialized = true;
  injectWizardStyles();
  buildShell();
  attachEvents();
  renderAll();
  loadModel();
}

function buildShell() {
  state.root.innerHTML = `
    <div class="dw-root">
      <div class="dw-profile-bar">
        <div class="dw-profile-list" id="dw_profile_list"></div>
        <div class="dw-profile-actions">
          <button class="secondary" data-action="profile-add">Add</button>
          <button class="secondary" data-action="profile-clone">Clone</button>
          <button class="secondary" data-action="profile-download">Download</button>
          <button class="secondary" data-action="profile-rename">Rename</button>
          <button class="danger" data-action="profile-delete">Delete</button>
        </div>
      </div>
      <div class="dw-meta-row">
        <div class="dw-field dw-schema-field">
          <label>Schema version</label>
          <input type="number" id="dw_schema" disabled>
        </div>
        <button class="secondary" data-action="toggle-json">Show JSON</button>
      </div>
      <div class="dw-toolbar" id="dw_toolbar">
        <button class="secondary" data-action="reload">Reload</button>
        <button class="secondary" data-action="add-device">Add device</button>
        <button class="secondary" data-action="open-wizard">Wizard</button>
        <button class="secondary" data-action="clone-device">Clone</button>
        <button class="danger" data-action="delete-device">Delete</button>
        <button class="primary" data-action="save">Save changes</button>
        <span class="dw-status" id="dw_status">Not loaded</span>
      </div>
      <div class="dw-validation hidden" id="dw_validation"></div>
      <div class="dw-layout">
        <div class="dw-list" id="dw_device_list"></div>
        <div class="dw-detail" id="dw_device_detail"></div>
      </div>
      <div class="dw-json collapsed" id="dw_json_panel">
        <pre class="dw-json-content" id="dw_json_preview">No config</pre>
      </div>
      <div class="dw-modal hidden" id="dw_track_picker_modal">
        <div class="dw-modal-content" id="dw_track_picker_content"></div>
      </div>
      <div class="dw-modal hidden" id="dw_wizard_modal">
        <div class="dw-modal-content" id="dw_wizard_content"></div>
      </div>
    </div>`;

  state.toolbar = document.getElementById('dw_toolbar');
  state.list = document.getElementById('dw_device_list');
  state.detail = document.getElementById('dw_device_detail');
  state.json = document.getElementById('dw_json_preview');
  state.jsonWrap = document.getElementById('dw_json_panel');
  state.statusEl = document.getElementById('dw_status');
  state.validationEl = document.getElementById('dw_validation');
  state.trackPickerModal = document.getElementById('dw_track_picker_modal');
  state.trackPickerContent = document.getElementById('dw_track_picker_content');
  state.wizardModal = document.getElementById('dw_wizard_modal');
  state.wizardContent = document.getElementById('dw_wizard_content');
  state.profileList = document.getElementById('dw_profile_list');
  state.profileActions = state.root.querySelector('.dw-profile-actions');
}

function attachEvents() {
  state.toolbar?.addEventListener('click', (ev) => {
    const btn = ev.target.closest('[data-action]');
    if (!btn) return;
    switch (btn.dataset.action) {
      case 'reload':
        loadModel();
        break;
      case 'add-device':
        addDevice();
        break;
      case 'clone-device':
        cloneDevice();
        break;
      case 'delete-device':
        deleteDevice();
        break;
      case 'open-wizard':
        openWizard();
        break;
      case 'save':
        saveModel();
        break;
      default:
        break;
    }
  });

  state.profileActions?.addEventListener('click', (ev) => {
    const btn = ev.target.closest('[data-action]');
    if (!btn) return;
    switch (btn.dataset.action) {
      case 'profile-add':
        createProfile();
        break;
      case 'profile-clone':
        cloneProfile();
        break;
      case 'profile-download':
        downloadProfile();
        break;
      case 'profile-rename':
        renameProfile();
        break;
      case 'profile-delete':
        deleteProfile();
        break;
      default:
        break;
    }
  });

  const jsonToggle = state.root.querySelector('[data-action="toggle-json"]');
  jsonToggle?.addEventListener('click', toggleJsonPanel);
  state.profileList?.addEventListener('click', (ev) => {
    const chip = ev.target.closest('[data-profile-id]');
    if (!chip) return;
    activateProfile(chip.dataset.profileId || '');
  });
  state.actionsRoot?.addEventListener('click', (ev) => {
    const btn = ev.target.closest('[data-run-device][data-run-scenario]');
    if (btn) {
      runScenario(btn.dataset.runDevice, btn.dataset.runScenario);
      return;
    }
    const signalResetBtn = ev.target.closest('[data-signal-reset-device]');
    if (signalResetBtn) {
      resetSignal(signalResetBtn.dataset.signalResetDevice, signalResetBtn);
      return;
    }
    const resetBtn = ev.target.closest('[data-sequence-reset-device]');
    if (!resetBtn) return;
    resetSequence(resetBtn.dataset.sequenceResetDevice, resetBtn);
  });

  state.list?.addEventListener('click', (ev) => {
    const item = ev.target.closest('[data-device-index]');
    if (!item) return;
    const idx = parseInt(item.dataset.deviceIndex, 10);
    if (!isNaN(idx)) {
      state.selectedDevice = idx;
      state.selectedScenario = -1;
      renderAll();
    }
  });

  state.detail?.addEventListener('input', handleDetailInput);
  state.detail?.addEventListener('change', handleDetailInput);
  state.detail?.addEventListener('click', handleDetailClick);

  state.trackPickerModal?.addEventListener('click', handleTrackPickerClick);
  state.trackPickerModal?.addEventListener('input', handleTrackPickerInput);
  state.wizardModal?.addEventListener('click', handleWizardClick);
  state.wizardModal?.addEventListener('input', handleWizardInput);
}

function toggleJsonPanel() {
  state.jsonVisible = !state.jsonVisible;
  if (state.jsonWrap) {
    state.jsonWrap.classList.toggle('collapsed', !state.jsonVisible);
  }
  const btn = state.root?.querySelector('[data-action="toggle-json"]');
  if (btn) {
    btn.textContent = state.jsonVisible ? 'Hide JSON' : 'Show JSON';
  }
  renderJson();
}

function handleDetailClick(ev) {
  const btn = ev.target.closest('[data-action]');
  if (!btn) return;
  const action = btn.dataset.action;
  switch (action) {
    case 'add-scenario': addScenario(); break;
    case 'remove-scenario': removeScenario(btn.dataset.index); break;
    case 'select-scenario': selectScenario(btn.dataset.index); break;
    case 'add-step': addStep(btn.dataset.stepType); break;
    case 'remove-step': removeStep(btn.dataset.index); break;
    case 'step-up': moveStep(btn.dataset.index, -1); break;
    case 'step-down': moveStep(btn.dataset.index, 1); break;
    case 'add-wait-rule': addWaitRule(btn.dataset.stepIndex); break;
    case 'remove-wait-rule': removeWaitRule(btn.dataset.stepIndex, btn.dataset.reqIndex); break;
    case 'slot-add': addTemplateSlot(); break;
    case 'slot-remove': removeTemplateSlot(btn.dataset.index); break;
    case 'mqtt-rule-add': addMqttRule(); break;
    case 'mqtt-rule-remove': removeMqttRule(btn.dataset.index); break;
    case 'flag-rule-add': addFlagRule(); break;
    case 'flag-rule-remove': removeFlagRule(btn.dataset.index); break;
    case 'condition-rule-add': addConditionRule(); break;
    case 'condition-rule-remove': removeConditionRule(btn.dataset.index); break;
    case 'sequence-step-add': addSequenceStep(); break;
    case 'sequence-step-remove': removeSequenceStep(btn.dataset.index); break;
    case 'template-new-scenario': addScenarioForReference(btn.dataset.templateField, btn.dataset.subfield, btn.dataset.index); break;
    case 'wait-add-known-flag': addKnownWaitFlag(btn.dataset.stepIndex, btn.dataset.flag); break;
    case 'track-picker-open': openTrackPicker(btn.closest('.dw-track-field')?.querySelector('input')); break;
    case 'track-picker-clear': clearTrackField(btn.closest('.dw-track-field')?.querySelector('input')); break;
  }
}

function handleDetailInput(ev) {
  const el = ev.target;
  if (!el) return;
  if (el.dataset.deviceField) {
    updateDeviceField(el.dataset.deviceField, el.value);
    return;
  }
  if (el.dataset.scenarioField) {
    updateScenarioField(el.dataset.scenarioField, el);
    return;
  }
  if (el.dataset.stepField) {
    updateStepField(el.dataset.index, el.dataset.stepField, el);
    return;
  }
  if (el.dataset.templateField) {
    updateTemplateField(el);
    return;
  }
  if (el.dataset.waitField) {
    updateWaitField(el.dataset.stepIndex, el.dataset.reqIndex, el.dataset.waitField, el);
  }
  validateRequiredFields();
}
function loadModel() {
  setStatus('Loading...', '#fbbf24');
  state.busy = true;
  fetch('/api/devices/config')
    .then(r => {
      if (!r.ok) throw new Error('HTTP ' + r.status);
      return r.json();
    })
    .then(cfg => {
      state.model = normalizeLoadedConfig(cfg || {});
      state.profiles = Array.isArray(cfg?.profiles) ? cfg.profiles : [];
      state.activeProfile = cfg?.active_profile || cfg?.activeProfile || '';
      if (!state.activeProfile && state.profiles.length) {
        state.activeProfile = state.profiles[0].id;
      }
      state.selectedDevice = (state.model.devices && state.model.devices.length) ? 0 : -1;
      state.selectedScenario = -1;
      state.dirty = false;
      state.busy = false;
      renderAll();
      setStatus('Loaded', '#22c55e');
    })
    .catch(err => {
      console.error(err);
      state.busy = false;
      setStatus('Load failed', '#f87171');
    });
}

function saveModel() {
  if (!state.model || state.busy) return;
  updateValidationState();
  if ((state.validation?.errors || []).length) {
    renderValidationOverview();
    setStatus(`Fix validation errors before saving (${state.validation.errors.length})`, '#f87171');
    return;
  }
  setStatus('Saving...', '#fbbf24');
  state.busy = true;
  const payload = prepareConfigForSave(state.model);
  const profileQuery = state.activeProfile ? `?profile=${encodeURIComponent(state.activeProfile)}` : '';
  fetch(`/api/devices/apply${profileQuery}`, {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify(payload),
  })
    .then(r => {
      if (!r.ok) throw new Error('HTTP ' + r.status);
      return r.json().catch(() => ({}));
    })
    .then(() => {
      state.busy = false;
      state.dirty = false;
      setStatus('Saved', '#22c55e');
      renderActions();
    })
    .catch(err => {
      state.busy = false;
      setStatus('Save failed: ' + err.message, '#f87171');
    });
}

function markDirty() {
  state.dirty = true;
  updateValidationState();
  renderJson();
  renderValidationOverview();
  updateToolbar();
  renderActions();
}

function renderAll() {
  updateValidationState();
  renderDeviceList();
  renderDeviceDetail();
  renderJson();
  renderValidationOverview();
  updateToolbar();
  updateGlobals();
  renderProfiles();
  renderActions();
}

function ensureModel() {
  if (!state.model) {
    state.model = {schema: 1, devices: []};
  }
  if (!Array.isArray(state.model.devices)) {
    state.model.devices = [];
  }
}

function getDevices() {
  ensureModel();
  return state.model.devices;
}

function currentDevice() {
  ensureModel();
  return state.model.devices[state.selectedDevice] || null;
}

function currentScenario() {
  const dev = currentDevice();
  if (!dev || !Array.isArray(dev.scenarios)) {
    return null;
  }
  return dev.scenarios[state.selectedScenario] || null;
}
function renderDeviceList() {
  if (!state.list) return;
  const devices = getDevices();
  if (!devices.length) {
    state.list.innerHTML = "<div class='dw-list-empty'>No devices</div>";
    state.selectedDevice = -1;
    state.selectedScenario = -1;
    if (state.detail) {
      state.detail.innerHTML = "<div class='dw-empty'>No devices configured yet.</div>";
    }
    return;
  }
  state.list.innerHTML = devices.map((dev, idx) => {
    const active = idx === state.selectedDevice ? ' active' : '';
    const name = escapeHtml(dev.display_name || dev.id || ('Device ' + (idx + 1)));
    const scenarios = (dev.scenarios || []).length;
    return `<div class="dw-device-item${active}" data-device-index="${idx}">
      <span>${name}</span>
      <span class="dw-badge">${scenarios}</span>
    </div>`;
  }).join('');
}

function renderDeviceDetail() {
  if (!state.detail) return;
  const dev = currentDevice();
  if (!dev) {
    state.detail.innerHTML = "<div class='dw-empty'>Select or add a device to edit.</div>";
    return;
  }
  state.detail.innerHTML = `
    <div class="dw-section">
      <h4>Basics</h4>
      <div class="dw-field required">
        <label>Device ID</label>
        <input data-device-field="id" value="${escapeAttr(dev.id || '')}" placeholder="unique-id" data-required="true">
        <div class="dw-hint small">Уникальный ID, используется для запуска сценариев.</div>
      </div>
      <div class="dw-field">
        <label>Display name</label>
        <input data-device-field="display_name" value="${escapeAttr(dev.display_name || '')}" placeholder="Visible name">
      </div>
    </div>
  ${renderTemplateSection(dev)}
  ${renderScenariosSection(dev)}
  ${renderFlagLookupDatalist(dev)}
  `;
  validateRequiredFields();
  refreshTrackPickers();
}

function renderTrackField(label, attrs, value, placeholder) {
  return `
    <div class="dw-field">
      <label>${escapeHtml(label)}</label>
      <div class="dw-track-field">
        <input ${attrs} value="${escapeAttr(value || '')}" placeholder="${escapeAttr(placeholder || '')}">
        <button class="secondary small" type="button" data-action="track-picker-open">Choose</button>
        <button class="secondary small" type="button" data-action="track-picker-clear">Clear</button>
      </div>
    </div>`;
}

function collectKnownFlags(dev) {
  const flags = new Set();
  const addFlag = (value) => {
    const flag = toSafeString(value).trim();
    if (flag) {
      flags.add(flag);
    }
  };
  if (!dev || typeof dev !== 'object') {
    return [];
  }
  (dev.template?.flag?.rules || []).forEach((rule) => addFlag(rule?.flag));
  (dev.template?.condition?.rules || []).forEach((rule) => addFlag(rule?.flag));
  (dev.scenarios || []).forEach((scen) => {
    (scen?.steps || []).forEach((step) => {
      if (step?.type === 'set_flag') {
        addFlag(step?.data?.flag?.flag || step?.flag);
      } else if (step?.type === 'wait_flags') {
        (step?.data?.wait_flags?.requirements || []).forEach((req) => addFlag(req?.flag));
      }
    });
  });
  return Array.from(flags).sort((a, b) => a.localeCompare(b));
}

function renderFlagLookupDatalist(dev) {
  const flags = collectKnownFlags(dev);
  if (!flags.length) {
    return '';
  }
  const options = flags.map((flag) => `<option value="${escapeAttr(flag)}"></option>`).join('');
  return `<datalist id="flag_lookup">${options}</datalist>`;
}

function collectTemplateScenarioRefs(dev) {
  const refs = new Map();
  if (!dev || !dev.template) {
    return refs;
  }
  const addRef = (scenarioId, source) => {
    const id = toSafeString(scenarioId).trim();
    if (!id) {
      return;
    }
    if (!refs.has(id)) {
      refs.set(id, {sources: []});
    }
    const ref = refs.get(id);
    if (!ref.sources.includes(source)) {
      ref.sources.push(source);
    }
  };

  switch (dev.template.type) {
    case 'on_mqtt_event':
      (dev.template.mqtt?.rules || []).forEach((rule, idx) => addRef(rule?.scenario, `MQTT trigger ${idx + 1}`));
      break;
    case 'on_flag':
      (dev.template.flag?.rules || []).forEach((rule, idx) => addRef(rule?.scenario, `Flag trigger ${idx + 1}`));
      break;
    case 'if_condition':
      addRef(dev.template.condition?.true_scenario, 'Condition branch: TRUE');
      addRef(dev.template.condition?.false_scenario, 'Condition branch: FALSE');
      break;
    case 'interval_task':
      addRef(dev.template.interval?.scenario, 'Interval action');
      break;
    case 'sequence_lock':
      addRef(dev.template.sequence?.success_scenario, 'Sequence result: success');
      addRef(dev.template.sequence?.fail_scenario, 'Sequence result: fail');
      break;
    default:
      break;
  }

  return refs;
}

function renderScenarioBadges(meta) {
  const badges = [];
  if (meta?.templateLinked) {
    badges.push(`<span class="dw-chip required">required</span>`);
    badges.push(`<span class="dw-chip linked">used by template</span>`);
  } else {
    badges.push(`<span class="dw-chip manual">manual</span>`);
  }
  return badges.join('');
}

function collectScenarioUsage(dev, scen) {
  const usage = [];
  const scenarioId = toSafeString(scen?.id).trim();
  if (!dev || !scenarioId) {
    return usage;
  }
  const templateRef = collectTemplateScenarioRefs(dev).get(scenarioId);
  if (templateRef?.sources?.length) {
    templateRef.sources.forEach((source) => usage.push(source));
  }
  if (scen?.button_enabled) {
    usage.push('Actions tab button');
  }
  return usage;
}

function renderScenarioListGroup(title, items) {
  return `
    <div class="dw-scenario-group">
      <div class="dw-scenario-group-title">${escapeHtml(title)}</div>
      ${items || "<div class='dw-list-empty'>No scenarios</div>"}
    </div>`;
}

function renderScenarioRefField(dev, label, field, subfield, value, index) {
  const scenarios = Array.isArray(dev?.scenarios) ? dev.scenarios : [];
  const currentValue = toSafeString(value);
  const options = [`<option value="">Select scenario</option>`].concat(
    scenarios.map((scen) => {
      const scenarioId = toSafeString(scen?.id).trim();
      if (!scenarioId) {
        return '';
      }
      const scenarioName = toSafeString(scen?.name).trim();
      const optionLabel = scenarioName && scenarioName !== scenarioId
        ? `${scenarioName} (${scenarioId})`
        : scenarioId;
      return `<option value="${escapeAttr(scenarioId)}" ${scenarioId === currentValue ? 'selected' : ''}>${escapeHtml(optionLabel)}</option>`;
    }).filter(Boolean)
  ).join('');
  const safeIndex = typeof index === 'number' && !Number.isNaN(index) ? ` data-index="${index}"` : '';
  return `
    <div class="dw-field">
      <label>${label}</label>
      <div class="dw-inline-actions">
        <select data-template-field="${field}" data-subfield="${subfield}"${safeIndex}>${options}</select>
        <button class="secondary small" type="button" data-action="template-new-scenario" data-template-field="${field}" data-subfield="${subfield}"${safeIndex}>+ New</button>
      </div>
      <div class="dw-hint small">Uses scenarios from this device. Manual ID stays in the scenario editor.</div>
    </div>`;
}

function validateRequiredFields() {
  if (!state.detail) return;
  const requiredControls = state.detail.querySelectorAll('[data-required="true"]');
  requiredControls.forEach((control) => {
    const field = control.closest('.dw-field');
    if (!field) {
      return;
    }
    const rule = control.dataset.requiredRule || '';
    let valid = true;
    if (rule === 'positive') {
      const num = parseInt(control.value, 10);
      valid = !Number.isNaN(num) && num > 0;
    } else {
      valid = (control.value || '').trim().length > 0;
    }
    field.classList.toggle('invalid', !valid);
  });
}

function renderTemplateSection(dev) {
  const tplType = dev.template?.type || '';
  const options = TEMPLATE_TYPES.map((tpl) => `<option value="${tpl.value}" ${tpl.value === tplType ? 'selected' : ''}>${tpl.label}</option>`).join('');
  const hint = templateTypeHint(tplType);
  let body = "<div class='dw-empty small'>Template is not set.</div>";
  if (tplType === 'uid_validator') {
    body = renderUidTemplate(dev);
  } else if (tplType === 'signal_hold') {
    body = renderSignalTemplate(dev);
  } else if (tplType === 'on_mqtt_event') {
    body = renderMqttTemplate(dev);
  } else if (tplType === 'on_flag') {
    body = renderFlagTemplate(dev);
  } else if (tplType === 'if_condition') {
    body = renderConditionTemplate(dev);
  } else if (tplType === 'interval_task') {
    body = renderIntervalTemplate(dev);
  } else if (tplType === 'sequence_lock') {
    body = renderSequenceTemplate(dev);
  }
  return `
    <div class="dw-section">
      <div class="dw-section-head">
        <h4>Template</h4>
      </div>
      <div class="dw-field">
        <label>Template type</label>
        <select data-template-field="type">${options}</select>
        <div class="dw-hint small">${escapeHtml(hint)}</div>
      </div>
      ${body}
    </div>`;
}

function templateTypeHint(type) {
  switch (type) {
    case 'uid_validator':
      return 'Validate UID input, react to success or failure, and optionally control background audio.';
    case 'signal_hold':
      return 'Keep a signal active for a period and optionally play hold or completion audio.';
    case 'on_mqtt_event':
      return 'Watch MQTT messages and launch scenarios when a trigger rule matches.';
    case 'on_flag':
      return 'Watch automation flags and run scenarios when a flag trigger matches.';
    case 'if_condition':
      return 'Evaluate a condition and branch into TRUE or FALSE scenarios.';
    case 'interval_task':
      return 'Run one scenario on a fixed interval.';
    case 'sequence_lock':
      return 'Guide a multi-step sequence with success and failure branches.';
    default:
      return 'Choose a template if this device should react automatically instead of only exposing manual scenarios.';
  }
}

function renderUidTemplate(dev) {
  ensureUidTemplate(dev);
  const tpl = dev.template?.uid || {slots: []};
  const slots = (tpl.slots || []).map((slot, idx) => {
    const last = slot.last_value ? escapeAttr(slot.last_value) : '';
    return `
    <div class="dw-slot">
      <div class="dw-slot-head">Slot ${idx + 1}<button class="danger small" data-action="slot-remove" data-index="${idx}">&times;</button></div>
      <div class="dw-field"><label>Source topic</label><input data-template-field="uid-slot" data-subfield="source_id" data-index="${idx}" value="${escapeAttr(slot.source_id || '')}" placeholder="sensor/topic"></div>
      <div class="dw-field"><label>Label</label><input data-template-field="uid-slot" data-subfield="label" data-index="${idx}" value="${escapeAttr(slot.label || '')}" placeholder="Friendly label"></div>
      <div class="dw-field"><label>Allowed values</label><input data-template-field="uid-values" data-index="${idx}" value="${escapeAttr((slot.values || []).join(', '))}" placeholder="uid1, uid2"></div>
      <div class="dw-field dw-slot-last"><label>Last read</label><div class="dw-last-value">${last || '&mdash;'}</div></div>
    </div>`;
  }).join('');
  return `
    <div class="dw-section">
      <h5>Activation</h5>
      <div class="dw-field required">
        <label>Wait for topic</label>
        <input data-template-field="uid-activation" data-subfield="start_topic" value="${escapeAttr(tpl.start_topic || '')}" placeholder="pictures/cmd/scan1" data-required="true">
        <div class="dw-hint small">Когда приходит этот топик (опц. вместе с payload), начинаем проверку UID.</div>
      </div>
      <div class="dw-field">
        <label>Match payload</label>
        <input data-template-field="uid-activation" data-subfield="start_payload" value="${escapeAttr(tpl.start_payload || '')}" placeholder="leave empty for any payload">
      </div>
      <div class="dw-field">
        <label>Broadcast topic</label>
        <input data-template-field="uid-activation" data-subfield="broadcast_topic" value="${escapeAttr(tpl.broadcast_topic || '')}" placeholder="pictures/cmd/scan">
        <div class="dw-hint small">Этот топик отправим сразу после получения стартовой команды.</div>
      </div>
      <div class="dw-field">
        <label>Broadcast payload</label>
        <input data-template-field="uid-activation" data-subfield="broadcast_payload" value="${escapeAttr(tpl.broadcast_payload || '')}">
      </div>
    </div>
    <div class="dw-section">
      <div class="dw-section-head">
        <span>UID slots</span>
        <button data-action="slot-add">Add slot</button>
      </div>
      ${slots || "<div class='dw-empty small'>No slots configured.</div>"}
    </div>
    <div class="dw-section">
      <h5>Success actions</h5>
      <div class="dw-field"><label>MQTT topic</label><input data-template-field="uid-action" data-subfield="success_topic" value="${escapeAttr(tpl.success_topic || '')}" placeholder="quest/ok"></div>
      <div class="dw-field"><label>Payload</label><input data-template-field="uid-action" data-subfield="success_payload" value="${escapeAttr(tpl.success_payload || '')}" placeholder="payload"></div>
      ${renderTrackField('Audio track', `data-template-field="uid-action" data-subfield="success_audio_track"`, tpl.success_audio_track || '', `${TRACK_LOOKUP_ROOT}/ok.mp3`)}
    </div>
    <div class="dw-section">
      <h5>Fail actions</h5>
      <div class="dw-field"><label>MQTT topic</label><input data-template-field="uid-action" data-subfield="fail_topic" value="${escapeAttr(tpl.fail_topic || '')}" placeholder="quest/fail"></div>
      <div class="dw-field"><label>Payload</label><input data-template-field="uid-action" data-subfield="fail_payload" value="${escapeAttr(tpl.fail_payload || '')}" placeholder="payload"></div>
      ${renderTrackField('Audio track', `data-template-field="uid-action" data-subfield="fail_audio_track"`, tpl.fail_audio_track || '', `${TRACK_LOOKUP_ROOT}/fail.mp3`)}
    </div>`;
}

function renderSignalTemplate(dev) {
  ensureSignalTemplate(dev);
  const sig = dev.template?.signal || {};
  return `
    <div class="dw-section">
      <h5>Signal control</h5>
      <div class="dw-field required"><label>Signal topic</label><input data-template-field="signal" data-subfield="signal_topic" value="${escapeAttr(sig.signal_topic || '')}" placeholder="quest/relay/cmd" data-required="true"><div class="dw-hint small">Топик для отправки ON/OFF при завершении удержания.</div></div>
      <div class="dw-field"><label>Payload ON</label><input data-template-field="signal" data-subfield="signal_payload_on" value="${escapeAttr(sig.signal_payload_on || '')}" placeholder="ON"></div>
      <div class="dw-field"><label>Payload OFF</label><input data-template-field="signal" data-subfield="signal_payload_off" value="${escapeAttr(sig.signal_payload_off || '')}" placeholder="OFF"></div>
      <div class="dw-field required"><label>Heartbeat topic</label><input data-template-field="signal" data-subfield="heartbeat_topic" value="${escapeAttr(sig.heartbeat_topic || '')}" placeholder="quest/relay/hb" data-required="true"><div class="dw-hint small">Топик, куда устройство шлёт heartbeat пока луч активен.</div></div>
      <div class="dw-field"><label>Reset topic</label><input data-template-field="signal" data-subfield="reset_topic" value="${escapeAttr(sig.reset_topic || '')}" placeholder="laser/reset"><div class="dw-hint small">Опубликуйте любое сообщение сюда, чтобы остановить трек и обнулить прогресс удержания.</div></div>
      <div class="dw-field required"><label>Required hold ms</label><input type="number" data-template-field="signal" data-subfield="required_hold_ms" value="${sig.required_hold_ms || 0}" data-required="true" data-required-rule="positive"><div class="dw-hint small">Минимальная длительность удержания в миллисекундах.</div></div>
      <div class="dw-field"><label>Heartbeat timeout ms</label><input type="number" data-template-field="signal" data-subfield="heartbeat_timeout_ms" value="${sig.heartbeat_timeout_ms || 0}"></div>
      ${renderTrackField('Hold track', `data-template-field="signal" data-subfield="hold_track"`, sig.hold_track || '', `${TRACK_LOOKUP_ROOT}/hold.mp3`)}
      <div class="dw-field"><label>Loop hold track</label><select data-template-field="signal" data-subfield="hold_track_loop"><option value="false" ${sig.hold_track_loop ? '' : 'selected'}>No</option><option value="true" ${sig.hold_track_loop ? 'selected' : ''}>Yes</option></select></div>
      ${renderTrackField('Complete track', `data-template-field="signal" data-subfield="complete_track"`, sig.complete_track || '', `${TRACK_LOOKUP_ROOT}/done.mp3`)}
    </div>`;
}

function renderMqttTemplate(dev) {
  ensureMqttTemplate(dev);
  const tpl = dev.template?.mqtt || {rules: []};
  const rules = (tpl.rules || []).map((rule, idx) => {
    const checked = rule.payload_required ? 'checked' : '';
    const topicText = toSafeString(rule.topic).trim() || 'any topic';
    const payloadText = rule.payload_required
      ? (toSafeString(rule.payload).trim() || 'exact payload')
      : 'any payload';
    const scenarioText = toSafeString(rule.scenario).trim() || 'no scenario selected';
    return `
    <div class="dw-slot">
      <div class="dw-slot-head">MQTT trigger ${idx + 1}<button class="danger small" data-action="mqtt-rule-remove" data-index="${idx}">&times;</button></div>
      <div class="dw-hint small">When topic <strong>${escapeHtml(topicText)}</strong> arrives with <strong>${escapeHtml(payloadText)}</strong>, run <strong>${escapeHtml(scenarioText)}</strong>.</div>
      <div class="dw-field"><label>Trigger label</label><input data-template-field="mqtt-rule" data-subfield="name" data-index="${idx}" value="${escapeAttr(rule.name || '')}" placeholder="Optional note for this trigger"></div>
      <div class="dw-field required"><label>Topic filter</label><input data-template-field="mqtt-rule" data-subfield="topic" data-index="${idx}" value="${escapeAttr(rule.topic || '')}" placeholder="sensor/topic" data-required="true"></div>
      <div class="dw-field"><label>Payload filter</label><input data-template-field="mqtt-rule" data-subfield="payload" data-index="${idx}" value="${escapeAttr(rule.payload || '')}" placeholder="payload"></div>
      <div class="dw-field dw-checkbox-field"><label><input type="checkbox" data-template-field="mqtt-rule" data-subfield="payload_required" data-index="${idx}" ${checked}>Require payload match</label></div>
      ${renderScenarioRefField(dev, 'Run scenario', 'mqtt-rule', 'scenario', rule.scenario || '', idx)}
    </div>`;
  }).join('');
  return `
    <div class="dw-section">
      <div class="dw-section-head">
        <span>MQTT triggers</span>
        <button data-action="mqtt-rule-add">Add trigger</button>
      </div>
      <div class="dw-hint small">Use this template for inbound MQTT-driven scenarios. It replaces the old per-device topic bindings.</div>
      ${rules || "<div class='dw-empty small'>No rules configured.</div>"}
      <div class="dw-hint small">Leave payload filtering disabled to react to any payload on the selected topic.</div>
    </div>`;
}

function renderFlagTemplate(dev) {
  ensureFlagTemplate(dev);
  const tpl = dev.template?.flag || {rules: []};
  const rules = (tpl.rules || []).map((rule, idx) => {
    return `
    <div class="dw-slot">
      <div class="dw-slot-head">Rule ${idx + 1}<button class="danger small" data-action="flag-rule-remove" data-index="${idx}">&times;</button></div>
      <div class="dw-field"><label>Name</label><input data-template-field="flag-rule" data-subfield="name" data-index="${idx}" value="${escapeAttr(rule.name || '')}" placeholder="Optional"></div>
      <div class="dw-field"><label>Flag</label><input data-template-field="flag-rule" data-subfield="flag" data-index="${idx}" value="${escapeAttr(rule.flag || '')}" placeholder="door_open"></div>
      <div class="dw-field">
        <label>Trigger when</label>
        <select data-template-field="flag-rule" data-subfield="state" data-index="${idx}">
          <option value="true" ${rule.required_state ? 'selected' : ''}>Flag becomes TRUE</option>
          <option value="false" ${!rule.required_state ? 'selected' : ''}>Flag becomes FALSE</option>
        </select>
      </div>
      ${renderScenarioRefField(dev, 'Scenario', 'flag-rule', 'scenario', rule.scenario || '', idx)}
    </div>`;
  }).join('');
  return `
    <div class="dw-section">
      <div class="dw-section-head">
        <span>Flag rules</span>
        <button data-action="flag-rule-add">Add rule</button>
      </div>
      ${rules || "<div class='dw-empty small'>No rules configured.</div>"}
      <div class="dw-hint small">Scenarios must exist on this device and will run when the selected flag changes.</div>
    </div>`;
}

function renderConditionTemplate(dev) {
  ensureConditionTemplate(dev);
  const tpl = dev.template?.condition || {};
  const rules = (tpl.rules || []).map((rule, idx) => `
    <div class="dw-slot">
      <div class="dw-slot-head">Condition ${idx + 1}<button class="danger small" data-action="condition-rule-remove" data-index="${idx}">&times;</button></div>
      <div class="dw-field"><label>Flag</label><input data-template-field="condition-rule" data-subfield="flag" data-index="${idx}" value="${escapeAttr(rule.flag || '')}" placeholder="flag_name"></div>
      <div class="dw-field">
        <label>State</label>
        <select data-template-field="condition-rule" data-subfield="state" data-index="${idx}">
          <option value="true" ${rule.required_state ? 'selected' : ''}>TRUE</option>
          <option value="false" ${!rule.required_state ? 'selected' : ''}>FALSE</option>
        </select>
      </div>
    </div>`).join('');
  return `
    <div class="dw-section">
      <div class="dw-section-head"><span>Condition settings</span></div>
      <div class="dw-field">
        <label>Logic mode</label>
        <select data-template-field="condition-mode">
          <option value="all" ${tpl.mode === 'all' ? 'selected' : ''}>All conditions</option>
          <option value="any" ${tpl.mode === 'any' ? 'selected' : ''}>Any condition</option>
        </select>
      </div>
      ${renderScenarioRefField(dev, 'Scenario if TRUE', 'condition-scenario', 'true', tpl.true_scenario || '')}
      ${renderScenarioRefField(dev, 'Scenario if FALSE', 'condition-scenario', 'false', tpl.false_scenario || '')}
    </div>
    <div class="dw-section">
      <div class="dw-section-head">
        <span>Conditions</span>
        <button data-action="condition-rule-add">Add condition</button>
      </div>
      ${rules || "<div class='dw-empty small'>No conditions configured.</div>"}
      <div class="dw-hint small">Conditions evaluate automation flags; scenarios run when result changes.</div>
    </div>`;
}

function renderIntervalTemplate(dev) {
  ensureIntervalTemplate(dev);
  const tpl = dev.template?.interval || {};
  const intervalMs = (tpl.interval_ms && tpl.interval_ms > 0) ? tpl.interval_ms : 1000;
  return `
    <div class="dw-section">
      <div class="dw-section-head"><span>Interval task</span></div>
      <div class="dw-field"><label>Interval (ms)</label><input type="number" min="1" data-template-field="interval" data-subfield="interval_ms" value="${intervalMs}"></div>
      ${renderScenarioRefField(dev, 'Scenario', 'interval', 'scenario', tpl.scenario || '')}
      <div class="dw-hint small">Runs the selected scenario on a fixed interval.</div>
    </div>`;
}

function renderSequenceTemplate(dev) {
  ensureSequenceTemplate(dev);
  const tpl = dev.template?.sequence || {};
  const steps = (tpl.steps || []).map((step, idx) => {
    const checked = step.payload_required ? 'checked' : '';
    return `
      <div class="dw-slot">
        <div class="dw-slot-head">Step ${idx + 1}<button class="danger small" data-action="sequence-step-remove" data-index="${idx}">&times;</button></div>
        <div class="dw-field"><label>MQTT topic</label><input data-template-field="sequence-step" data-subfield="topic" data-index="${idx}" value="${escapeAttr(step.topic || '')}" placeholder="sensor/topic"></div>
        <div class="dw-field"><label>Payload</label><input data-template-field="sequence-step" data-subfield="payload" data-index="${idx}" value="${escapeAttr(step.payload || '')}" placeholder="payload"></div>
        <div class="dw-field dw-checkbox-field"><label><input type="checkbox" data-template-field="sequence-step" data-subfield="payload_required" data-index="${idx}" ${checked}>Require exact payload</label></div>
        <div class="dw-field"><label>Hint topic</label><input data-template-field="sequence-step" data-subfield="hint_topic" data-index="${idx}" value="${escapeAttr(step.hint_topic || '')}" placeholder="hint/topic"></div>
        <div class="dw-field"><label>Hint payload</label><input data-template-field="sequence-step" data-subfield="hint_payload" data-index="${idx}" value="${escapeAttr(step.hint_payload || '')}" placeholder="payload"></div>
        ${renderTrackField('Hint audio track', `data-template-field="sequence-step" data-subfield="hint_audio_track" data-index="${idx}"`, step.hint_audio_track || '', `${TRACK_LOOKUP_ROOT}/hint.mp3`)}
      </div>`;
  }).join('');
  return `
    <div class="dw-section">
      <div class="dw-section-head">
        <span>Sequence steps</span>
        <button data-action="sequence-step-add">Add step</button>
      </div>
      ${steps || "<div class='dw-empty small'>No steps defined.</div>"}
      <div class="dw-hint small">Steps advance when matching MQTT messages arrive in order.</div>
    </div>
    <div class="dw-section">
      <div class="dw-field"><label>Timeout (ms)</label><input type="number" min="0" step="100" data-template-field="sequence" data-subfield="timeout_ms" value="${tpl.timeout_ms || 0}" placeholder="0 = no timeout"></div>
      <div class="dw-field"><label class="dw-checkbox"><input type="checkbox" data-template-field="sequence" data-subfield="reset_on_error" ${tpl.reset_on_error !== false ? 'checked' : ''}>Reset on wrong step</label></div>
    </div>
    <div class="dw-section">
      <h5>Success actions</h5>
      <div class="dw-field"><label>MQTT topic</label><input data-template-field="sequence" data-subfield="success_topic" value="${escapeAttr(tpl.success_topic || '')}" placeholder="quest/sequence/success"></div>
      <div class="dw-field"><label>Payload</label><input data-template-field="sequence" data-subfield="success_payload" value="${escapeAttr(tpl.success_payload || '')}" placeholder="payload"></div>
      ${renderTrackField('Audio track', `data-template-field="sequence" data-subfield="success_audio_track"`, tpl.success_audio_track || '', `${TRACK_LOOKUP_ROOT}/success.mp3`)}
      ${renderScenarioRefField(dev, 'Scenario', 'sequence', 'success_scenario', tpl.success_scenario || '')}
    </div>
    <div class="dw-section">
      <h5>Fail actions</h5>
      <div class="dw-field"><label>MQTT topic</label><input data-template-field="sequence" data-subfield="fail_topic" value="${escapeAttr(tpl.fail_topic || '')}" placeholder="quest/sequence/fail"></div>
      <div class="dw-field"><label>Payload</label><input data-template-field="sequence" data-subfield="fail_payload" value="${escapeAttr(tpl.fail_payload || '')}" placeholder="payload"></div>
      ${renderTrackField('Audio track', `data-template-field="sequence" data-subfield="fail_audio_track"`, tpl.fail_audio_track || '', `${TRACK_LOOKUP_ROOT}/fail.mp3`)}
      ${renderScenarioRefField(dev, 'Scenario', 'sequence', 'fail_scenario', tpl.fail_scenario || '')}
      <div class="dw-hint small">Failure actions run when timeout expires or an unexpected topic arrives.</div>
    </div>`;
}

function renderScenariosSection(dev) {
  const templateRefs = collectTemplateScenarioRefs(dev);
  const templateItems = [];
  const customItems = [];
  (dev.scenarios || []).forEach((sc, idx) => {
    const active = idx === state.selectedScenario ? ' active' : '';
    const title = escapeHtml(sc.name || sc.id || ('Scenario ' + (idx + 1)));
    const scenarioId = toSafeString(sc?.id).trim();
    const refMeta = scenarioId ? templateRefs.get(scenarioId) : null;
    const item = `<div class="dw-scenario-item${active}" data-action="select-scenario" data-index="${idx}">
      <div class="dw-scenario-main">
        <span>${title}</span>
        <div class="dw-chip-row">${renderScenarioBadges({templateLinked: !!refMeta})}</div>
      </div>
      <span class="dw-badge">${(sc.steps || []).length}</span>
    </div>`;
    if (refMeta) {
      templateItems.push(item);
    } else {
      customItems.push(item);
    }
  });
  return `
    <div class="dw-section">
      <div class="dw-scenarios">
        <div class="dw-scenario-list">
          <div class="dw-scenario-actions">
            <button data-action="add-scenario">Add</button>
            <button class="danger" data-action="remove-scenario" data-index="${state.selectedScenario}">Delete</button>
          </div>
          ${renderScenarioListGroup('Template scenarios', templateItems.join(''))}
          ${renderScenarioListGroup('Custom scenarios', customItems.join(''))}
        </div>
        <div class="dw-scenario-detail">
          ${renderScenarioDetail()}
        </div>
      </div>
    </div>`;
}

function renderScenarioDetail() {
  const scen = currentScenario();
  const dev = currentDevice();
  if (!scen) {
    return "<div class='dw-empty'>Select a scenario to edit.</div>";
  }
  const scenarioId = toSafeString(scen.id).trim();
  const refMeta = scenarioId ? collectTemplateScenarioRefs(dev).get(scenarioId) : null;
  const usage = collectScenarioUsage(dev, scen);
  const steps = (scen.steps || []).map((step, idx) => renderStep(step, idx)).join('');
  const stepPresetGroups = [
    {title: 'Messaging', items: [
      ['mqtt_publish', 'MQTT', 'Publish topic and payload'],
      ['event', 'Event', 'Send internal bus event'],
    ]},
    {title: 'Audio', items: [
      ['audio_play', 'Audio Play', 'Start track playback'],
      ['audio_stop', 'Audio Stop', 'Stop current audio'],
    ]},
    {title: 'Control', items: [
      ['set_flag', 'Set Flag', 'Update automation flag'],
      ['wait_flags', 'Wait Flags', 'Pause until flags match'],
    ]},
    {title: 'Logic', items: [
      ['delay', 'Delay', 'Wait fixed time before next step'],
      ['loop', 'Loop', 'Jump back to earlier step'],
    ]},
  ].map((group) => {
    const buttons = group.items
      .map(([type, label, desc]) => `<button class="dw-step-preset-btn" type="button" data-action="add-step" data-step-type="${type}">
        <span class="dw-step-preset-label">+ ${escapeHtml(label)}</span>
        <span class="dw-step-preset-desc">${escapeHtml(desc)}</span>
      </button>`)
      .join('');
    return `<div class="dw-step-preset-group">
      <div class="dw-step-preset-title">${escapeHtml(group.title)}</div>
      <div class="dw-step-presets">${buttons}</div>
    </div>`;
  }).join('');
  return `
    <div class="dw-field">
      <label>Scenario type</label>
      <div class="dw-chip-row">${renderScenarioBadges({templateLinked: !!refMeta})}</div>
      ${refMeta ? `<div class="dw-hint small">Linked from: ${escapeHtml(refMeta.sources.join(', '))}</div>` : "<div class='dw-hint small'>Custom scenario not referenced by the current template.</div>"}
    </div>
    <div class="dw-field">
      <label>Used by</label>
      ${usage.length
        ? `<div class="dw-usage-list">${usage.map((item) => `<div class="dw-usage-item">${escapeHtml(item)}</div>`).join('')}</div>`
        : "<div class='dw-empty small'>Not referenced</div>"}
    </div>
    <div class="dw-field">
      <label>Scenario ID</label>
      <input data-scenario-field="id" value="${escapeAttr(scen.id || '')}" placeholder="scenario_id">
    </div>
    <div class="dw-field">
      <label>Scenario name</label>
      <input data-scenario-field="name" value="${escapeAttr(scen.name || '')}" placeholder="Display name">
    </div>
    <div class="dw-field dw-checkbox-field">
      <label><input type="checkbox" data-scenario-field="button_enabled" ${scen.button_enabled ? 'checked' : ''}>Show in Actions tab</label>
    </div>
    <div class="dw-field">
      <label>Button label</label>
      <input data-scenario-field="button_label" value="${escapeAttr(scen.button_label || scen.name || scen.id || '')}" placeholder="Friendly label" ${scen.button_enabled ? '' : 'disabled'}>
    </div>
    <div class="dw-step-footer">
      <div class="dw-step-preset-panel">${stepPresetGroups}</div>
      <span class="dw-note">Steps execute sequentially; use loops and waits for advanced flows.</span>
    </div>
    <div>${steps || "<div class='dw-empty'>No steps</div>"}</div>
  `;
}

function stepTypeLabel(type) {
  switch (type) {
    case 'mqtt_publish': return 'Publish MQTT';
    case 'audio_play': return 'Play audio';
    case 'audio_stop': return 'Stop audio';
    case 'set_flag': return 'Set flag';
    case 'wait_flags': return 'Wait for flags';
    case 'loop': return 'Loop';
    case 'delay': return 'Delay';
    case 'event': return 'Emit event';
    case 'nop':
    default:
      return 'No operation';
  }
}

function stepSummary(step) {
  const type = step?.type || 'nop';
  switch (type) {
    case 'mqtt_publish': {
      const topic = toSafeString(step?.data?.mqtt?.topic).trim();
      const payload = toSafeString(step?.data?.mqtt?.payload).trim();
      if (topic && payload) return `${topic} -> ${payload}`;
      if (topic) return topic;
      return 'Topic and payload';
    }
    case 'audio_play': {
      const track = toSafeString(step?.data?.audio?.track).trim();
      return track || 'Track playback';
    }
    case 'audio_stop':
      return 'Stop current audio output';
    case 'set_flag': {
      const flag = toSafeString(step?.data?.flag?.flag).trim() || 'flag';
      const value = step?.data?.flag?.value ? 'true' : 'false';
      return `${flag} = ${value}`;
    }
    case 'wait_flags': {
      const reqs = Array.isArray(step?.data?.wait_flags?.requirements) ? step.data.wait_flags.requirements : [];
      const mode = step?.data?.wait_flags?.mode === 'any' ? 'any' : 'all';
      if (!reqs.length) {
        return `Wait for ${mode} conditions`;
      }
      if (reqs.length === 1) {
        const req = reqs[0] || {};
        const flag = toSafeString(req.flag).trim() || 'flag';
        return `${flag} = ${req.required_state ? 'true' : 'false'}`;
      }
      return `Wait for ${mode} of ${reqs.length} flags`;
    }
    case 'loop': {
      const target = parseInt(step?.data?.loop?.target_step, 10);
      const maxIterations = parseInt(step?.data?.loop?.max_iterations, 10);
      const targetText = Number.isNaN(target) ? 'previous step' : `step ${target + 1}`;
      if (!Number.isNaN(maxIterations) && maxIterations > 0) {
        return `Back to ${targetText}, max ${maxIterations} times`;
      }
      return `Back to ${targetText}`;
    }
    case 'delay': {
      const delayMs = parseInt(step?.delay_ms, 10);
      return `${Number.isNaN(delayMs) ? 0 : delayMs} ms`;
    }
    case 'event': {
      const eventName = toSafeString(step?.data?.event?.event).trim();
      const topic = toSafeString(step?.data?.event?.topic).trim();
      if (eventName && topic) return `${eventName} on ${topic}`;
      if (eventName) return eventName;
      return 'Internal event';
    }
    case 'nop':
    default:
      return 'No action';
  }
}

function renderStep(step, idx) {
  const options = ACTION_TYPES.map((type) => `<option value="${type}" ${type === step.type ? 'selected' : ''}>${escapeHtml(stepTypeLabel(type))}</option>`).join('');
  const title = stepTypeLabel(step.type || 'nop');
  const summary = stepSummary(step);
  return `
    <div class="dw-step-card">
      <div class="dw-step-head">
        <div class="dw-step-title-wrap">
          <h5>${escapeHtml(title)}</h5>
          <div class="dw-step-summary">${escapeHtml(summary)}</div>
          <span class="dw-step-index">Step ${idx + 1}</span>
        </div>
        <div class="dw-step-head-controls">
          <button data-action="step-up" data-index="${idx}">&uarr;</button>
          <button data-action="step-down" data-index="${idx}">&darr;</button>
          <button class="danger" data-action="remove-step" data-index="${idx}">Delete</button>
        </div>
      </div>
      <div class="dw-step-body">
        <div class="dw-field">
          <label>Type</label>
          <select data-step-field="type" data-index="${idx}">${options}</select>
        </div>
        <div class="dw-field">
          <label>Delay ms</label>
          <input type="number" data-step-field="delay_ms" data-index="${idx}" value="${step.delay_ms || 0}">
        </div>
        ${renderStepFields(step, idx)}
      </div>
    </div>`;
}

function renderStepFields(step, idx) {
  const type = step.type || 'nop';
  switch (type) {
    case 'mqtt_publish':
      ensure(step, ['data','mqtt']);
      return `
        <div class="dw-field"><label>Topic</label><input data-step-field="data.mqtt.topic" data-index="${idx}" value="${escapeAttr(step.data.mqtt.topic || '')}"></div>
        <div class="dw-field"><label>Payload</label><input data-step-field="data.mqtt.payload" data-index="${idx}" value="${escapeAttr(step.data.mqtt.payload || '')}"></div>
        <div class="dw-field"><label>QoS</label><input type="number" data-step-field="data.mqtt.qos" data-index="${idx}" value="${step.data.mqtt.qos || 0}"></div>
        <div class="dw-field"><label>Retain</label><select data-step-field="data.mqtt.retain" data-index="${idx}"><option value="false" ${step.data.mqtt.retain?'':'selected'}>False</option><option value="true" ${step.data.mqtt.retain?'selected':''}>True</option></select></div>`;
    case 'audio_play':
      ensure(step, ['data','audio']);
      return `
        ${renderTrackField('Track path', `data-step-field="data.audio.track" data-index="${idx}"`, step.data.audio.track || '', `${TRACK_LOOKUP_ROOT}/track.mp3`)}
        <div class="dw-field"><label>Blocking</label><select data-step-field="data.audio.blocking" data-index="${idx}"><option value="false" ${step.data.audio.blocking?'':'selected'}>No</option><option value="true" ${step.data.audio.blocking?'selected':''}>Yes</option></select></div>`;
    case 'set_flag':
      ensure(step, ['data','flag']);
      return `
        <div class="dw-field"><label>Flag name</label><input data-step-field="data.flag.flag" data-index="${idx}" value="${escapeAttr(step.data.flag.flag || '')}" list="flag_lookup"></div>
        <div class="dw-field"><label>Value</label><select data-step-field="data.flag.value" data-index="${idx}"><option value="true" ${step.data.flag.value?'selected':''}>True</option><option value="false" ${step.data.flag.value?'':'selected'}>False</option></select></div>`;
    case 'wait_flags':
      ensure(step, ['data','wait_flags']);
      ensure(step.data.wait_flags, ['requirements']);
      const waitMode = step.data.wait_flags.mode === 'any' ? 'any' : 'all';
      const waitModeText = waitMode === 'any' ? 'Wait until any condition matches' : 'Wait until all conditions match';
      const knownFlags = collectKnownFlags(currentDevice());
      const knownFlagsHint = knownFlags.length
        ? `Known flags: ${escapeHtml(knownFlags.join(', '))}`
        : 'No known flags yet. Add flags in steps or template rules first.';
      const knownFlagButtons = knownFlags.map((flag) => `
        <button class="dw-flag-chip-btn" type="button" data-action="wait-add-known-flag" data-step-index="${idx}" data-flag="${escapeAttr(flag)}">
          + ${escapeHtml(flag)}
        </button>`).join('');
      const reqSummary = (step.data.wait_flags.requirements || []).map((req) => {
        const flag = escapeHtml(req.flag || 'flag');
        const stateLabel = req.required_state ? 'true' : 'false';
        return `<span class="dw-wait-chip">${flag} = ${stateLabel}</span>`;
      }).join('');
      const reqs = (step.data.wait_flags.requirements || []).map((req, rIdx) => `
        <div class="dw-wait-card">
          <div class="dw-wait-card-head">
            <span class="dw-wait-card-title">${escapeHtml((req.flag || 'Flag').trim() || 'Flag')} = ${req.required_state ? 'true' : 'false'}</span>
            <button data-action="remove-wait-rule" data-step-index="${idx}" data-req-index="${rIdx}">&times;</button>
          </div>
          <div class="dw-wait-row">
            <input placeholder="flag name" list="flag_lookup" data-wait-field="flag" data-step-index="${idx}" data-req-index="${rIdx}" value="${escapeAttr(req.flag || '')}">
            <select data-wait-field="state" data-step-index="${idx}" data-req-index="${rIdx}">
              <option value="true" ${req.required_state?'selected':''}>true</option>
              <option value="false" ${req.required_state?'':'selected'}>false</option>
            </select>
            <span class="dw-wait-preview">${escapeHtml((req.flag || 'flag').trim() || 'flag')} = ${req.required_state ? 'true' : 'false'}</span>
          </div>
        </div>`).join('');
      return `
        <div class="dw-field">
          <label>Wait until</label>
          <div class="dw-wait-panel">
            <div class="dw-wait-panel-head">
              <div class="dw-wait-headline">${waitModeText}</div>
              <div class="dw-wait-summary">${reqSummary || "<span class='dw-empty small'>No requirements yet</span>"}</div>
            </div>
            <div class="dw-wait-settings">
              <div class="dw-field"><label>Mode</label><select data-step-field="data.wait_flags.mode" data-index="${idx}">
                <option value="all" ${waitMode==='all'?'selected':''}>All conditions</option>
                <option value="any" ${waitMode==='any'?'selected':''}>Any condition</option>
              </select></div>
              <div class="dw-field"><label>Timeout ms</label><input type="number" data-step-field="data.wait_flags.timeout_ms" data-index="${idx}" value="${step.data.wait_flags.timeout_ms || 0}"></div>
            </div>
            <div class="dw-hint small">${knownFlagsHint}</div>
            ${knownFlagButtons ? `<div class="dw-known-flags">
              <div class="dw-known-flags-title">Use existing flags</div>
              <div class="dw-known-flags-list">${knownFlagButtons}</div>
            </div>` : ''}
            <div class="dw-field"><label>Requirements</label>
              <div class="dw-wait-req">${reqs || "<div class='dw-empty small'>No requirements</div>"}</div>
              <button class="secondary" data-action="add-wait-rule" data-step-index="${idx}">Add requirement</button>
            </div>
          </div>
        </div>`;
    case 'loop':
      ensure(step, ['data','loop']);
      return `
        <div class="dw-field"><label>Target step</label><input type="number" data-step-field="data.loop.target_step" data-index="${idx}" value="${step.data.loop.target_step || 0}"></div>
        <div class="dw-field"><label>Max iterations</label><input type="number" data-step-field="data.loop.max_iterations" data-index="${idx}" value="${step.data.loop.max_iterations || 0}"></div>`;
    case 'event':
      ensure(step, ['data','event']);
      return `
        <div class="dw-field"><label>Event</label><input data-step-field="data.event.event" data-index="${idx}" value="${escapeAttr(step.data.event.event || '')}"></div>
        <div class="dw-field"><label>Topic</label><input data-step-field="data.event.topic" data-index="${idx}" value="${escapeAttr(step.data.event.topic || '')}"></div>
        <div class="dw-field"><label>Payload</label><input data-step-field="data.event.payload" data-index="${idx}" value="${escapeAttr(step.data.event.payload || '')}"></div>`;
    default:
      return '';
  }
}
function ensure(obj, path) {
  let cursor = obj;
  for (const key of path) {
    if (cursor[key] === undefined) cursor[key] = {};
    cursor = cursor[key];
  }
  return cursor;
}

function slugify(text) {
  return String(text || '')
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, '-')
    .replace(/^-+|-+$/g, '')
    || `device_${Date.now().toString(16)}`;
}

function refreshRequiredIndicators() {
  if (typeof validateRequiredFields === 'function') {
    validateRequiredFields();
  }
}

function refreshTrackPickers() {
  if (!state.detail) {
    return;
  }
  const inputs = state.detail.querySelectorAll('.dw-track-field input');
  if (inputs.length) {
    ensureTrackLookupOptions();
  }
}

const TRACK_LOOKUP_ROOT = (typeof AUDIO_ROOT === 'string' && AUDIO_ROOT) ? AUDIO_ROOT : '/sdcard';
const TRACK_LOOKUP_MAX_DIRS = 48;
const TRACK_LOOKUP_MAX_TRACKS = 600;
let trackLookupCache = [];
let trackLookupPromise = null;

function ensureTrackLookupOptions(forceReload) {
  if (trackLookupPromise) {
    return trackLookupPromise;
  }
  if (!forceReload && trackLookupCache.length) {
    if (typeof updateTrackLookupOptions === 'function') {
      updateTrackLookupOptions(trackLookupCache);
    }
    return Promise.resolve(trackLookupCache);
  }
  trackLookupPromise = collectTrackLookupEntries(TRACK_LOOKUP_ROOT).then((tracks) => {
    trackLookupCache = tracks;
    if (typeof updateTrackLookupOptions === 'function') {
      updateTrackLookupOptions(trackLookupCache);
    }
    return trackLookupCache;
  }).catch((err) => {
    console.warn('Track lookup failed', err);
    return trackLookupCache;
  }).finally(() => {
    trackLookupPromise = null;
  });
  return trackLookupPromise;
}

function openTrackPicker(targetInput) {
  if (!targetInput || !state.trackPickerModal || !state.trackPickerContent) {
    return;
  }
  state.trackPicker.targetInput = targetInput;
  state.trackPicker.query = '';
  state.trackPicker.open = true;
  ensureTrackLookupOptions().finally(() => {
    renderTrackPicker();
    state.trackPickerModal.classList.remove('hidden');
    const search = state.trackPickerContent.querySelector('input[data-track-picker-field="query"]');
    if (search) {
      search.focus();
    }
  });
}

function closeTrackPicker() {
  state.trackPicker.open = false;
  state.trackPicker.query = '';
  state.trackPicker.targetInput = null;
  if (state.trackPickerModal) {
    state.trackPickerModal.classList.add('hidden');
  }
  if (state.trackPickerContent) {
    state.trackPickerContent.innerHTML = '';
  }
}

function getTrackPickerResults() {
  const query = toSafeString(state.trackPicker.query).trim().toLowerCase();
  const tracks = Array.isArray(trackLookupCache) ? trackLookupCache : [];
  if (!query) {
    return tracks;
  }
  return tracks.filter((entry) => {
    const path = toSafeString(entry?.path).toLowerCase();
    const name = trackBaseName(entry?.path).toLowerCase();
    const dir = trackDirName(entry?.path).toLowerCase();
    return path.includes(query) || name.includes(query) || dir.includes(query);
  });
}

function renderTrackPicker() {
  if (!state.trackPickerContent) {
    return;
  }
  const targetValue = toSafeString(state.trackPicker.targetInput?.value).trim();
  const results = getTrackPickerResults();
  const grouped = new Map();
  results.forEach((entry) => {
    const dir = trackDirName(entry?.path) || '/';
    if (!grouped.has(dir)) {
      grouped.set(dir, []);
    }
    grouped.get(dir).push(entry);
  });
  const groupsHtml = Array.from(grouped.entries()).map(([dir, entries]) => {
    const items = entries.map((entry) => {
      const path = toSafeString(entry?.path);
      const active = path === targetValue ? ' active' : '';
      return `<button type="button" class="dw-track-picker-item${active}" data-track-pick="${escapeAttr(path)}">
        <span class="dw-track-picker-name">${escapeHtml(trackBaseName(path) || path)}</span>
        <span class="dw-track-picker-path">${escapeHtml(path)}</span>
      </button>`;
    }).join('');
    return `<div class="dw-track-picker-group">
      <div class="dw-track-picker-group-title">${escapeHtml(dir)}</div>
      <div class="dw-track-picker-items">${items}</div>
    </div>`;
  }).join('');

  state.trackPickerContent.innerHTML = `
    <div class="dw-track-picker">
      <div class="dw-track-picker-head">
        <h3>Choose audio track</h3>
        <button type="button" class="secondary small" data-action="track-picker-close">Close</button>
      </div>
      <div class="dw-field">
        <label>Search</label>
        <input data-track-picker-field="query" value="${escapeAttr(state.trackPicker.query || '')}" placeholder="Search by file or folder">
      </div>
      <div class="dw-track-picker-current">
        <span class="dw-track-picker-current-label">Current</span>
        <span class="dw-track-picker-current-value">${escapeHtml(targetValue || 'No track selected')}</span>
      </div>
      <div class="dw-track-picker-results">
        ${groupsHtml || "<div class='dw-empty'>No tracks found</div>"}
      </div>
    </div>`;
}

function applyTrackPickerValue(value) {
  const input = state.trackPicker.targetInput;
  if (!input) {
    closeTrackPicker();
    return;
  }
  input.value = toSafeString(value);
  if (input.dataset.stepField) {
    updateStepField(input.dataset.index, input.dataset.stepField, input);
  } else if (input.dataset.templateField) {
    updateTemplateField(input);
  }
  renderDeviceDetail();
  markDirty();
  closeTrackPicker();
}

function clearTrackField(input) {
  if (!input) return;
  input.value = '';
  if (input.dataset.stepField) {
    updateStepField(input.dataset.index, input.dataset.stepField, input);
  } else if (input.dataset.templateField) {
    updateTemplateField(input);
  }
  renderDeviceDetail();
  markDirty();
}

function handleTrackPickerClick(ev) {
  if (ev.target === state.trackPickerModal) {
    closeTrackPicker();
    return;
  }
  const btn = ev.target.closest('[data-action], [data-track-pick]');
  if (!btn) {
    return;
  }
  if (btn.dataset.trackPick !== undefined) {
    applyTrackPickerValue(btn.dataset.trackPick);
    return;
  }
  if (btn.dataset.action === 'track-picker-close') {
    closeTrackPicker();
  }
}

function handleTrackPickerInput(ev) {
  const el = ev.target;
  if (!el || el.dataset.trackPickerField !== 'query') {
    return;
  }
  const caret = typeof el.selectionStart === 'number' ? el.selectionStart : null;
  state.trackPicker.query = el.value || '';
  renderTrackPicker();
  const nextInput = state.trackPickerContent?.querySelector('input[data-track-picker-field="query"]');
  if (nextInput) {
    nextInput.focus();
    if (caret !== null) {
      nextInput.setSelectionRange(caret, caret);
    }
  }
}

async function collectTrackLookupEntries(root) {
  const queue = [root];
  const visited = new Set();
  const tracks = [];
  while (queue.length && visited.size < TRACK_LOOKUP_MAX_DIRS && tracks.length < TRACK_LOOKUP_MAX_TRACKS) {
    const dir = queue.shift();
    if (!dir || visited.has(dir)) {
      continue;
    }
    visited.add(dir);
    const entries = await loadTrackDirectory(dir);
    entries.forEach((entry) => {
      if (!entry || !entry.path) {
        return;
      }
      if (entry.dir) {
        if (!visited.has(entry.path) && queue.length < TRACK_LOOKUP_MAX_DIRS) {
          queue.push(entry.path);
        }
      } else if (tracks.length < TRACK_LOOKUP_MAX_TRACKS) {
        tracks.push(entry);
      }
    });
  }
  return tracks.sort((a, b) => {
    const dirA = trackDirName(a.path);
    const dirB = trackDirName(b.path);
    if (dirA !== dirB) {
      return dirA.localeCompare(dirB);
    }
    return trackBaseName(a.path).localeCompare(trackBaseName(b.path));
  });
}

async function loadTrackDirectory(path) {
  try {
    const res = await fetch(`/api/files?path=${encodeURIComponent(path)}`);
    if (!res.ok) {
      throw new Error(`HTTP ${res.status}`);
    }
    const data = await res.json();
    return Array.isArray(data) ? data : [];
  } catch (err) {
    console.warn('Track lookup dir failed', path, err);
    return [];
  }
}

function trackBaseName(path) {
  if (!path) {
    return '';
  }
  const parts = path.split('/').filter(Boolean);
  return parts.length ? parts[parts.length - 1] : path;
}

function trackDirName(path) {
  if (!path) {
    return '/';
  }
  const idx = path.lastIndexOf('/');
  if (idx < 0) {
    return '/';
  }
  const dir = path.slice(0, idx) || '/';
  return dir || '/';
}

function updateDeviceField(field, value) {
  const dev = currentDevice();
  if (!dev) return;
  dev[field] = value;
  markDirty();
  refreshRequiredIndicators();
}

function updateTemplateField(el) {
  const dev = currentDevice();
  if (!dev) return;
  const field = el.dataset.templateField;
  switch (field) {
    case 'type':
      setDeviceTemplate(dev, el.value);
      return;
    case 'uid-slot': {
      ensureUidTemplate(dev);
      const idx = parseInt(el.dataset.index, 10);
      if (!dev.template?.uid || Number.isNaN(idx) || !dev.template.uid.slots[idx]) {
        return;
      }
      const sub = el.dataset.subfield;
      dev.template.uid.slots[idx][sub] = el.value;
      break;
    }
    case 'uid-values': {
      ensureUidTemplate(dev);
      const idx = parseInt(el.dataset.index, 10);
      if (!dev.template?.uid || Number.isNaN(idx) || !dev.template.uid.slots[idx]) {
        return;
      }
      dev.template.uid.slots[idx].values = el.value.split(',').map((v) => v.trim()).filter(Boolean);
      break;
    }
    case 'uid-action': {
      ensureUidTemplate(dev);
      if (!dev.template?.uid) return;
      const sub = el.dataset.subfield;
      dev.template.uid[sub] = el.value;
      break;
    }
    case 'uid-activation': {
      ensureUidTemplate(dev);
      if (!dev.template?.uid) return;
      const sub = el.dataset.subfield;
      dev.template.uid[sub] = el.value;
      break;
    }
    case 'signal': {
      ensureSignalTemplate(dev);
      if (!dev.template?.signal) return;
      const sub = el.dataset.subfield;
      if (sub === 'required_hold_ms' || sub === 'heartbeat_timeout_ms') {
        dev.template.signal[sub] = parseInt(el.value, 10) || 0;
      } else if (sub === 'hold_track_loop') {
        dev.template.signal[sub] = el.value === 'true';
      } else {
        dev.template.signal[sub] = el.value;
      }
      break;
    }
    case 'mqtt-rule': {
      ensureMqttTemplate(dev);
      const tpl = dev.template?.mqtt;
      if (!tpl) return;
      const idx = parseInt(el.dataset.index, 10);
      if (Number.isNaN(idx) || !tpl.rules[idx]) return;
      const sub = el.dataset.subfield;
      if (sub === 'payload_required') {
        tpl.rules[idx].payload_required = el.type === 'checkbox' ? el.checked : el.value === 'true';
      } else if (sub === 'scenario') {
        if (!setTemplateScenarioReference(dev, 'mqtt-rule', sub, el.dataset.index, el.value)) return;
      } else {
        tpl.rules[idx][sub] = el.value;
      }
      break;
    }
    case 'flag-rule': {
      ensureFlagTemplate(dev);
      const tpl = dev.template?.flag;
      if (!tpl) return;
      const idx = parseInt(el.dataset.index, 10);
      if (Number.isNaN(idx) || !tpl.rules[idx]) return;
      const sub = el.dataset.subfield;
      if (sub === 'state') {
        tpl.rules[idx].required_state = el.value === 'true';
      } else if (sub === 'scenario') {
        if (!setTemplateScenarioReference(dev, 'flag-rule', sub, el.dataset.index, el.value)) return;
      } else {
        tpl.rules[idx][sub] = el.value;
      }
      break;
    }
    case 'condition-mode': {
      ensureConditionTemplate(dev);
      if (!dev.template?.condition) return;
      dev.template.condition.mode = el.value === 'any' ? 'any' : 'all';
      break;
    }
    case 'condition-scenario': {
      ensureConditionTemplate(dev);
      if (!setTemplateScenarioReference(dev, 'condition-scenario', el.dataset.subfield, '', el.value)) return;
      break;
    }
    case 'condition-rule': {
      ensureConditionTemplate(dev);
      const tpl = dev.template?.condition;
      if (!tpl) return;
      const idx = parseInt(el.dataset.index, 10);
      if (Number.isNaN(idx) || !tpl.rules[idx]) return;
      const sub = el.dataset.subfield;
      if (sub === 'state') {
        tpl.rules[idx].required_state = el.value === 'true';
      } else {
        tpl.rules[idx][sub] = el.value;
      }
      break;
    }
    case 'interval': {
      ensureIntervalTemplate(dev);
      if (!dev.template?.interval) return;
      const sub = el.dataset.subfield;
      if (sub === 'interval_ms') {
        const val = parseInt(el.value, 10);
        const next = Number.isNaN(val) ? 1000 : Math.max(val, 1);
        dev.template.interval.interval_ms = next;
        el.value = next;
      } else if (sub === 'scenario') {
        if (!setTemplateScenarioReference(dev, 'interval', sub, '', el.value)) return;
      }
      break;
    }
    case 'sequence-step': {
      ensureSequenceTemplate(dev);
      const tpl = dev.template?.sequence;
      if (!tpl) return;
      const idx = parseInt(el.dataset.index, 10);
      if (Number.isNaN(idx) || !tpl.steps[idx]) return;
      const sub = el.dataset.subfield;
      if (sub === 'payload_required') {
        tpl.steps[idx].payload_required = el.type === 'checkbox' ? el.checked : el.value === 'true';
      } else {
        tpl.steps[idx][sub] = el.value;
      }
      break;
    }
    case 'sequence': {
      ensureSequenceTemplate(dev);
      const tpl = dev.template?.sequence;
      if (!tpl) return;
      const sub = el.dataset.subfield;
      if (sub === 'timeout_ms') {
        const val = parseInt(el.value, 10);
        const next = Number.isNaN(val) ? 0 : Math.max(val, 0);
        tpl.timeout_ms = next;
        el.value = next;
      } else if (sub === 'reset_on_error') {
        tpl.reset_on_error = el.type === 'checkbox' ? el.checked : el.value === 'true';
      } else if (sub === 'success_scenario' || sub === 'fail_scenario') {
        if (!setTemplateScenarioReference(dev, 'sequence', sub, '', el.value)) return;
      } else {
        tpl[sub] = el.value;
      }
      break;
    }
    default:
      return;
  }
  markDirty();
  refreshRequiredIndicators();
}

function setTemplateScenarioReference(dev, field, subfield, indexStr, value) {
  if (!dev) return false;
  const nextValue = toSafeString(value);
  switch (field) {
    case 'mqtt-rule': {
      ensureMqttTemplate(dev);
      const tpl = dev.template?.mqtt;
      const idx = parseInt(indexStr, 10);
      if (!tpl || Number.isNaN(idx) || !tpl.rules[idx] || subfield !== 'scenario') return false;
      tpl.rules[idx].scenario = nextValue;
      return true;
    }
    case 'flag-rule': {
      ensureFlagTemplate(dev);
      const tpl = dev.template?.flag;
      const idx = parseInt(indexStr, 10);
      if (!tpl || Number.isNaN(idx) || !tpl.rules[idx] || subfield !== 'scenario') return false;
      tpl.rules[idx].scenario = nextValue;
      return true;
    }
    case 'condition-scenario': {
      ensureConditionTemplate(dev);
      if (!dev.template?.condition) return false;
      if (subfield === 'true') {
        dev.template.condition.true_scenario = nextValue;
        return true;
      }
      if (subfield === 'false') {
        dev.template.condition.false_scenario = nextValue;
        return true;
      }
      return false;
    }
    case 'interval': {
      ensureIntervalTemplate(dev);
      if (!dev.template?.interval || subfield !== 'scenario') return false;
      dev.template.interval.scenario = nextValue;
      return true;
    }
    case 'sequence': {
      ensureSequenceTemplate(dev);
      const tpl = dev.template?.sequence;
      if (!tpl) return false;
      if (subfield === 'success_scenario' || subfield === 'fail_scenario') {
        tpl[subfield] = nextValue;
        return true;
      }
      return false;
    }
    default:
      return false;
  }
}

function updateScenarioField(field, el) {
  const scen = currentScenario();
  if (!scen || !el) return;
  switch (field) {
    case 'id': {
      const dev = currentDevice();
      const prevId = toSafeString(scen.id);
      const nextId = toSafeString(el.value);
      scen.id = nextId;
      if (dev && prevId !== nextId) {
        updateScenarioReferencesForRename(dev, prevId, nextId);
      }
      break;
    }
    case 'button_enabled': {
      scen.button_enabled = el.type === 'checkbox' ? el.checked : el.value === 'true';
      if (!scen.button_enabled) {
        scen.button_label = '';
      }
      renderDeviceDetail();
      break;
    }
    case 'button_label':
      scen.button_label = el.value;
      break;
    default:
      scen[field] = el.value;
      break;
  }
  markDirty();
}

function replaceScenarioRefValue(container, key, prevId, nextId) {
  if (!container || !key) {
    return;
  }
  if (toSafeString(container[key]) === prevId) {
    container[key] = nextId;
  }
}

function updateScenarioReferencesForRename(dev, prevId, nextId) {
  if (!dev || !prevId || prevId === nextId || !dev.template) {
    return;
  }
  switch (dev.template.type) {
    case 'on_mqtt_event':
      (dev.template.mqtt?.rules || []).forEach((rule) => replaceScenarioRefValue(rule, 'scenario', prevId, nextId));
      break;
    case 'on_flag':
      (dev.template.flag?.rules || []).forEach((rule) => replaceScenarioRefValue(rule, 'scenario', prevId, nextId));
      break;
    case 'if_condition':
      replaceScenarioRefValue(dev.template.condition, 'true_scenario', prevId, nextId);
      replaceScenarioRefValue(dev.template.condition, 'false_scenario', prevId, nextId);
      break;
    case 'interval_task':
      replaceScenarioRefValue(dev.template.interval, 'scenario', prevId, nextId);
      break;
    case 'sequence_lock':
      replaceScenarioRefValue(dev.template.sequence, 'success_scenario', prevId, nextId);
      replaceScenarioRefValue(dev.template.sequence, 'fail_scenario', prevId, nextId);
      break;
    default:
      break;
  }
}

function updateStepField(indexStr, field, el) {
  const idx = parseInt(indexStr, 10);
  const scen = currentScenario();
  if (!scen || isNaN(idx) || !scen.steps || !scen.steps[idx]) return;
  const step = scen.steps[idx];
  if (field === 'type') {
    step.type = el.value;
    renderDeviceDetail();
    markDirty();
    return;
  }
  if (field.includes('.')) {
    const parts = field.split('.');
    let target = step;
    for (let i = 0; i < parts.length - 1; i++) {
      const key = parts[i];
      if (target[key] === undefined) target[key] = {};
      target = target[key];
    }
    target[parts[parts.length - 1]] = normalizeValue(el.value, el.type);
  } else {
    step[field] = normalizeValue(el.value, el.type);
  }
  markDirty();
}

function updateWaitField(stepIdxStr, reqIdxStr, field, el) {
  const stepIdx = parseInt(stepIdxStr, 10);
  const reqIdx = parseInt(reqIdxStr, 10);
  const scen = currentScenario();
  if (!scen || isNaN(stepIdx) || isNaN(reqIdx)) return;
  const step = scen.steps?.[stepIdx];
  if (!step) return;
  ensure(step, ['data','wait_flags','requirements']);
  const req = step.data.wait_flags.requirements[reqIdx];
  if (!req) return;
  if (field === 'flag') req.flag = el.value;
  if (field === 'state') req.required_state = el.value === 'true';
  markDirty();
}

function setDeviceTemplate(dev, type) {
  if (!dev) return;
  let nextType = type || '';
  const allowed = ['uid_validator',
                   'signal_hold',
                   'on_mqtt_event',
                   'on_flag',
                   'if_condition',
                   'interval_task',
                   'sequence_lock'];
  if (!allowed.includes(nextType)) {
    dev.template = null;
    markDirty();
    renderDeviceDetail();
    return;
  }
  if (!dev.template || dev.template.type !== nextType) {
    if (nextType === 'uid_validator') {
      dev.template = {
        type: nextType,
        uid: defaultUidTemplate(),
      };
    } else if (nextType === 'signal_hold') {
      dev.template = {
        type: nextType,
        signal: defaultSignalTemplate(),
      };
    } else if (nextType === 'on_mqtt_event') {
      dev.template = {
        type: nextType,
        mqtt: defaultMqttTemplate(),
      };
    } else if (nextType === 'on_flag') {
      dev.template = {
        type: nextType,
        flag: defaultFlagTemplate(),
      };
    } else if (nextType === 'if_condition') {
      dev.template = {
        type: nextType,
        condition: defaultConditionTemplate(),
      };
    } else if (nextType === 'interval_task') {
      dev.template = {
        type: nextType,
        interval: defaultIntervalTemplate(),
      };
    } else if (nextType === 'sequence_lock') {
      dev.template = {
        type: nextType,
        sequence: defaultSequenceTemplate(),
      };
    }
  }
  if (nextType === 'uid_validator') {
    ensureUidTemplate(dev);
  } else if (nextType === 'signal_hold') {
    ensureSignalTemplate(dev);
  } else if (nextType === 'on_mqtt_event') {
    ensureMqttTemplate(dev);
  } else if (nextType === 'on_flag') {
    ensureFlagTemplate(dev);
  } else if (nextType === 'if_condition') {
    ensureConditionTemplate(dev);
  } else if (nextType === 'interval_task') {
    ensureIntervalTemplate(dev);
  } else if (nextType === 'sequence_lock') {
    ensureSequenceTemplate(dev);
  }
  markDirty();
  renderDeviceDetail();
}

function defaultUidTemplate() {
  return {
    slots: [],
    start_topic: '',
    start_payload: '',
    broadcast_topic: '',
    broadcast_payload: '',
    success_topic: '',
    success_payload: '',
    success_audio_track: '',
    fail_topic: '',
    fail_payload: '',
    fail_audio_track: '',
  };
}

function defaultSignalTemplate() {
  return {
    signal_topic: '',
    signal_payload_on: '',
    signal_payload_off: '',
    signal_on_ms: 0,
    heartbeat_topic: '',
    reset_topic: '',
    required_hold_ms: 0,
    heartbeat_timeout_ms: 0,
    hold_track: '',
    hold_track_loop: false,
    complete_track: '',
  };
}

function defaultMqttTemplate() {
  return {
    rules: [],
  };
}

function defaultFlagTemplate() {
  return {
    rules: [],
  };
}

function defaultConditionTemplate() {
  return {
    mode: 'all',
    rules: [],
    true_scenario: '',
    false_scenario: '',
  };
}

function defaultIntervalTemplate() {
  return {
    interval_ms: 1000,
    scenario: '',
  };
}

function defaultSequenceTemplate() {
  return {
    steps: [],
    timeout_ms: 0,
    reset_on_error: true,
    success_topic: '',
    success_payload: '',
    success_audio_track: '',
    success_scenario: '',
    fail_topic: '',
    fail_payload: '',
    fail_audio_track: '',
    fail_scenario: '',
  };
}

function ensureUidTemplate(dev) {
  if (!dev || !dev.template || dev.template.type !== 'uid_validator') {
    return;
  }
  if (!dev.template.uid) {
    dev.template.uid = defaultUidTemplate();
  }
  const tpl = dev.template.uid;
  if (!Array.isArray(tpl.slots)) {
    tpl.slots = [];
  }
  tpl.slots.forEach((slot) => {
    slot.source_id = slot.source_id || '';
    slot.label = slot.label || '';
    if (!Array.isArray(slot.values)) {
      slot.values = [];
    }
  });
  ['success_topic','success_payload','success_audio_track','fail_topic','fail_payload','fail_audio_track'].forEach((key) => {
    if (typeof tpl[key] !== 'string') {
      tpl[key] = '';
    }
  });
  tpl.start_topic = tpl.start_topic || '';
  tpl.start_payload = tpl.start_payload || '';
  tpl.broadcast_topic = tpl.broadcast_topic || '';
  tpl.broadcast_payload = tpl.broadcast_payload || '';
}

function ensureSignalTemplate(dev) {
  if (!dev || !dev.template || dev.template.type !== 'signal_hold') {
    return;
  }
  if (!dev.template.signal) {
    dev.template.signal = defaultSignalTemplate();
  }
  const sig = dev.template.signal;
  ['signal_topic','signal_payload_on','signal_payload_off','heartbeat_topic','reset_topic','hold_track','complete_track'].forEach((key) => {
    sig[key] = sig[key] || '';
  });
  sig.required_hold_ms = sig.required_hold_ms || 0;
  sig.heartbeat_timeout_ms = sig.heartbeat_timeout_ms || 0;
  sig.hold_track_loop = !!sig.hold_track_loop;
  sig.signal_on_ms = sig.signal_on_ms || 0;
}



function ensureMqttTemplate(dev) {
  if (!dev || !dev.template || dev.template.type !== 'on_mqtt_event') {
    return;
  }
  if (!dev.template.mqtt) {
    dev.template.mqtt = defaultMqttTemplate();
  }
  const tpl = dev.template.mqtt;
  if (!Array.isArray(tpl.rules)) {
    tpl.rules = [];
  }
  tpl.rules.forEach((rule) => {
    rule.name = rule.name || '';
    rule.topic = rule.topic || '';
    rule.payload = rule.payload || '';
    rule.scenario = rule.scenario || '';
    rule.payload_required = !!rule.payload_required;
  });
}

function ensureFlagTemplate(dev) {
  if (!dev || !dev.template || dev.template.type !== 'on_flag') {
    return;
  }
  if (!dev.template.flag) {
    dev.template.flag = defaultFlagTemplate();
  }
  const tpl = dev.template.flag;
  if (!Array.isArray(tpl.rules)) {
    tpl.rules = [];
  }
  tpl.rules.forEach((rule) => {
    rule.name = rule.name || '';
    rule.flag = rule.flag || '';
    rule.scenario = rule.scenario || '';
    rule.required_state = rule.required_state !== undefined ? !!rule.required_state : true;
  });
}

function ensureConditionTemplate(dev) {
  if (!dev || !dev.template || dev.template.type !== 'if_condition') {
    return;
  }
  if (!dev.template.condition) {
    dev.template.condition = defaultConditionTemplate();
  }
  const tpl = dev.template.condition;
  tpl.mode = tpl.mode === 'any' ? 'any' : 'all';
  if (!Array.isArray(tpl.rules)) {
    tpl.rules = [];
  }
  tpl.true_scenario = tpl.true_scenario || '';
  tpl.false_scenario = tpl.false_scenario || '';
  tpl.rules.forEach((rule) => {
    rule.flag = rule.flag || '';
    if (rule.required_state === undefined) {
      rule.required_state = true;
    }
  });
}

function ensureIntervalTemplate(dev) {
  if (!dev || !dev.template || dev.template.type !== 'interval_task') {
    return;
  }
  if (!dev.template.interval) {
    dev.template.interval = defaultIntervalTemplate();
  }
  if (typeof dev.template.interval.interval_ms !== 'number') {
    dev.template.interval.interval_ms = 1000;
  }
  dev.template.interval.scenario = dev.template.interval.scenario || '';
}

function ensureSequenceTemplate(dev) {
  if (!dev || !dev.template || dev.template.type !== 'sequence_lock') {
    return;
  }
  if (!dev.template.sequence) {
    dev.template.sequence = defaultSequenceTemplate();
  }
  const seq = dev.template.sequence;
  if (!Array.isArray(seq.steps)) {
    seq.steps = [];
  }
  seq.steps.forEach((step) => {
    step.topic = step.topic || '';
    step.payload = step.payload || '';
    step.payload_required = !!step.payload_required;
    step.hint_topic = step.hint_topic || '';
    step.hint_payload = step.hint_payload || '';
    step.hint_audio_track = step.hint_audio_track || '';
  });
  seq.timeout_ms = seq.timeout_ms || 0;
  if (seq.reset_on_error === undefined) {
    seq.reset_on_error = true;
  }
  ['success_topic','success_payload','success_audio_track','success_scenario','fail_topic','fail_payload','fail_audio_track','fail_scenario'].forEach((key) => {
    if (typeof seq[key] !== 'string') {
      seq[key] = '';
    }
  });
}

function addTemplateSlot() {
  const dev = currentDevice();
  if (!dev || dev.template?.type !== 'uid_validator') {
    return;
  }
  ensureUidTemplate(dev);
  dev.template.uid.slots.push({source_id: '', label: '', values: []});
  markDirty();
  renderDeviceDetail();
}

function removeTemplateSlot(indexStr) {
  const dev = currentDevice();
  if (!dev || dev.template?.type !== 'uid_validator') {
    return;
  }
  ensureUidTemplate(dev);
  const idx = parseInt(indexStr, 10);
  if (Number.isNaN(idx) || idx < 0) {
    return;
  }
  dev.template.uid.slots.splice(idx, 1);
  markDirty();
  renderDeviceDetail();
}

function addMqttRule() {
  const dev = currentDevice();
  if (!dev || dev.template?.type !== 'on_mqtt_event') {
    return;
  }
  ensureMqttTemplate(dev);
  const tpl = dev.template.mqtt;
  if (tpl.rules.length >= MQTT_RULE_LIMIT) {
    setStatus('MQTT rule limit reached', '#fbbf24');
    return;
  }
  tpl.rules.push({
    name: '',
    topic: '',
    payload: '',
    payload_required: true,
    scenario: '',
  });
  markDirty();
  renderDeviceDetail();
}

function removeMqttRule(indexStr) {
  const dev = currentDevice();
  if (!dev || dev.template?.type !== 'on_mqtt_event') {
    return;
  }
  ensureMqttTemplate(dev);
  const idx = parseInt(indexStr, 10);
  if (Number.isNaN(idx)) {
    return;
  }
  dev.template.mqtt.rules.splice(idx, 1);
  markDirty();
  renderDeviceDetail();
}

function addFlagRule() {
  const dev = currentDevice();
  if (!dev || dev.template?.type !== 'on_flag') {
    return;
  }
  ensureFlagTemplate(dev);
  const tpl = dev.template.flag;
  if (tpl.rules.length >= FLAG_RULE_LIMIT) {
    setStatus('Flag rule limit reached', '#fbbf24');
    return;
  }
  tpl.rules.push({
    name: '',
    flag: '',
    scenario: '',
    required_state: true,
  });
  markDirty();
  renderDeviceDetail();
}

function removeFlagRule(indexStr) {
  const dev = currentDevice();
  if (!dev || dev.template?.type !== 'on_flag') {
    return;
  }
  ensureFlagTemplate(dev);
  const idx = parseInt(indexStr, 10);
  if (Number.isNaN(idx)) {
    return;
  }
  dev.template.flag.rules.splice(idx, 1);
  markDirty();
  renderDeviceDetail();
}

function addConditionRule() {
  const dev = currentDevice();
  if (!dev || dev.template?.type !== 'if_condition') {
    return;
  }
  ensureConditionTemplate(dev);
  const tpl = dev.template.condition;
  if (tpl.rules.length >= FLAG_RULE_LIMIT) {
    setStatus('Condition limit reached', '#fbbf24');
    return;
  }
  tpl.rules.push({
    flag: '',
    required_state: true,
  });
  markDirty();
  renderDeviceDetail();
}

function removeConditionRule(indexStr) {
  const dev = currentDevice();
  if (!dev || dev.template?.type !== 'if_condition') {
    return;
  }
  ensureConditionTemplate(dev);
  const idx = parseInt(indexStr, 10);
  if (Number.isNaN(idx)) {
    return;
  }
  dev.template.condition.rules.splice(idx, 1);
  markDirty();
  renderDeviceDetail();
}

function addSequenceStep() {
  const dev = currentDevice();
  if (!dev || dev.template?.type !== 'sequence_lock') {
    return;
  }
  ensureSequenceTemplate(dev);
  const tpl = dev.template.sequence;
  if (tpl.steps.length >= SEQUENCE_STEP_LIMIT) {
    setStatus('Step limit reached', '#fbbf24');
    return;
  }
  tpl.steps.push({
    topic: '',
    payload: '',
    payload_required: false,
    hint_topic: '',
    hint_payload: '',
    hint_audio_track: '',
  });
  markDirty();
  renderDeviceDetail();
}

function removeSequenceStep(indexStr) {
  const dev = currentDevice();
  if (!dev || dev.template?.type !== 'sequence_lock') {
    return;
  }
  ensureSequenceTemplate(dev);
  const idx = parseInt(indexStr, 10);
  if (Number.isNaN(idx)) {
    return;
  }
  dev.template.sequence.steps.splice(idx, 1);
  markDirty();
  renderDeviceDetail();
}

function addWaitRule(stepIdxStr) {
  const idx = parseInt(stepIdxStr, 10);
  const scen = currentScenario();
  if (!scen || isNaN(idx)) return;
  const step = scen.steps?.[idx];
  if (!step) return;
  ensure(step, ['data','wait_flags','requirements']);
  step.data.wait_flags.requirements.push({flag: '', required_state: true});
  renderDeviceDetail();
  markDirty();
}

function removeWaitRule(stepIdxStr, reqIdxStr) {
  const stepIdx = parseInt(stepIdxStr, 10);
  const reqIdx = parseInt(reqIdxStr, 10);
  const scen = currentScenario();
  if (!scen || isNaN(stepIdx) || isNaN(reqIdx)) return;
  const step = scen.steps?.[stepIdx];
  if (!step) return;
  ensure(step, ['data','wait_flags','requirements']);
  step.data.wait_flags.requirements.splice(reqIdx, 1);
  renderDeviceDetail();
  markDirty();
}

function addKnownWaitFlag(stepIdxStr, flagName) {
  const stepIdx = parseInt(stepIdxStr, 10);
  const scen = currentScenario();
  const flag = toSafeString(flagName).trim();
  if (!scen || isNaN(stepIdx) || !flag) return;
  const step = scen.steps?.[stepIdx];
  if (!step) return;
  ensure(step, ['data', 'wait_flags', 'requirements']);
  const reqs = step.data.wait_flags.requirements;
  const emptyReq = Array.isArray(reqs) ? reqs.find((req) => req && !toSafeString(req.flag).trim()) : null;
  if (emptyReq) {
    emptyReq.flag = flag;
    if (emptyReq.required_state === undefined) {
      emptyReq.required_state = true;
    }
  } else {
    reqs.push({flag, required_state: true});
  }
  renderDeviceDetail();
  markDirty();
}

function normalizeValue(value, type) {
  if (type === 'number') {
    const num = parseInt(value, 10);
    return isNaN(num) ? 0 : num;
  }
  if (value === 'true') return true;
  if (value === 'false') return false;
  return value;
}

function normalizeLoadedConfig(cfg) {
  const model = cfg && typeof cfg === 'object' ? cfg : {devices: []};
  if (!Array.isArray(model.devices)) {
    model.devices = [];
  }
  model.devices.forEach(normalizeDevice);
  return model;
}

function normalizeDevice(dev) {
  if (!dev || typeof dev !== 'object') return;
  if (!dev.display_name) {
    dev.display_name = dev.name || dev.id || '';
  }
  if (Object.prototype.hasOwnProperty.call(dev, 'name')) {
    delete dev.name;
  }
  if (Object.prototype.hasOwnProperty.call(dev, 'topics')) {
    delete dev.topics;
  }
  if (!Array.isArray(dev.scenarios)) {
    dev.scenarios = [];
  }
  dev.scenarios.forEach(normalizeScenario);
  if (dev.template && dev.template.type === 'signal_hold') {
    ensureSignalTemplate(dev);
  }
}

function normalizeScenario(scen) {
  if (!scen || typeof scen !== 'object') return;
  scen.button_enabled = !!scen.button_enabled;
  if (typeof scen.button_label !== 'string') {
    scen.button_label = '';
  }
  if (!Array.isArray(scen.steps)) {
    scen.steps = [];
  }
  scen.steps.forEach(normalizeStepForEditing);
}

function normalizeStepForEditing(step) {
  if (!step || typeof step !== 'object') return;
  step.data = step.data || {};
  switch (step.type) {
    case 'mqtt_publish': {
      const mqtt = step.data.mqtt = step.data.mqtt || {};
      if (mqtt.topic === undefined) mqtt.topic = step.topic || '';
      if (mqtt.payload === undefined) mqtt.payload = step.payload || '';
      if (mqtt.qos === undefined) mqtt.qos = typeof step.qos === 'number' ? step.qos : 0;
      if (mqtt.retain === undefined) mqtt.retain = !!step.retain;
      break;
    }
    case 'audio_play': {
      const audio = step.data.audio = step.data.audio || {};
      if (audio.track === undefined) audio.track = step.track || '';
      if (audio.blocking === undefined) audio.blocking = !!step.blocking;
      break;
    }
    case 'set_flag': {
      const flag = step.data.flag = step.data.flag || {};
      if (flag.flag === undefined) flag.flag = step.flag || '';
      if (flag.value === undefined) flag.value = step.value !== undefined ? !!step.value : false;
      break;
    }
    case 'wait_flags': {
      const waitFlags = step.data.wait_flags = step.data.wait_flags || {};
      if (waitFlags.mode === undefined) waitFlags.mode = (step.wait && step.wait.mode) || 'all';
      if (waitFlags.timeout_ms === undefined) {
        waitFlags.timeout_ms = (step.wait && step.wait.timeout_ms) ? step.wait.timeout_ms : 0;
      }
      const requirements = Array.isArray(waitFlags.requirements)
        ? waitFlags.requirements
        : (step.wait && Array.isArray(step.wait.requirements) ? step.wait.requirements : []);
      waitFlags.requirements = requirements.map(req => ({
        flag: req.flag || '',
        required_state: req.required_state !== undefined ? !!req.required_state : !!req.state,
      }));
      break;
    }
    case 'loop': {
      const loop = step.data.loop = step.data.loop || {};
      if (loop.target_step === undefined) {
        loop.target_step = (step.loop && typeof step.loop.target_step === 'number') ? step.loop.target_step : 0;
      }
      if (loop.max_iterations === undefined) {
        loop.max_iterations = (step.loop && typeof step.loop.max_iterations === 'number') ? step.loop.max_iterations : 0;
      }
      break;
    }
    case 'event': {
      const event = step.data.event = step.data.event || {};
      if (event.event === undefined) event.event = step.event || '';
      if (event.topic === undefined) event.topic = step.topic || '';
      if (event.payload === undefined) event.payload = step.payload || '';
      break;
    }
    default:
      break;
  }
}
function prepareConfigForSave(model) {
  const snapshot = JSON.parse(JSON.stringify(model || {}));
  if (!Array.isArray(snapshot.devices)) {
    snapshot.devices = [];
  }
  snapshot.devices.forEach((dev, devIdx) => {
    if (!dev || typeof dev !== 'object') return;
    const displayName = toSafeString(dev.display_name || dev.id || 'Device');
    if (!Array.isArray(dev.scenarios)) {
      snapshot.devices[devIdx].scenarios = [];
    } else {
      snapshot.devices[devIdx].scenarios = dev.scenarios.map(serializeScenarioForSave);
    }
    snapshot.devices[devIdx].display_name = displayName;
    if (Object.prototype.hasOwnProperty.call(snapshot.devices[devIdx], 'name')) {
      delete snapshot.devices[devIdx].name;
    }
    if (Object.prototype.hasOwnProperty.call(snapshot.devices[devIdx], 'topics')) {
      delete snapshot.devices[devIdx].topics;
    }
    stripTemplateRuntimeFields(snapshot.devices[devIdx]);
    if (snapshot.devices[devIdx].tabs) {
      delete snapshot.devices[devIdx].tabs;
    }
  });
  if (snapshot.tab_limit !== undefined) {
    delete snapshot.tab_limit;
  }
  return snapshot;
}

function makeValidationIssue(level, message, meta) {
  return Object.assign({level, message}, meta || {});
}

function pushValidationIssue(bucket, level, message, meta) {
  bucket.push(makeValidationIssue(level, message, meta));
}

function validateScenarioReference(issues, dev, devIdx, scenarioIds, value, label, meta) {
  const scenarioId = toSafeString(value).trim();
  if (!scenarioId) {
    pushValidationIssue(issues.warnings, 'warning', `${label} is empty`, Object.assign({deviceIndex: devIdx}, meta || {}));
    return;
  }
  if (!scenarioIds.has(scenarioId)) {
    pushValidationIssue(
      issues.errors,
      'error',
      `${label} points to missing scenario "${scenarioId}"`,
      Object.assign({deviceIndex: devIdx}, meta || {})
    );
  }
}

function collectValidationIssues(model) {
  const issues = {errors: [], warnings: []};
  const devices = Array.isArray(model?.devices) ? model.devices : [];
  const deviceIdOwners = new Map();

  devices.forEach((dev, devIdx) => {
    const deviceId = toSafeString(dev?.id).trim();
    if (!deviceId) {
      pushValidationIssue(issues.errors, 'error', 'Device ID is required', {deviceIndex: devIdx});
    } else {
      if (!deviceIdOwners.has(deviceId)) {
        deviceIdOwners.set(deviceId, []);
      }
      deviceIdOwners.get(deviceId).push(devIdx);
    }

    const scenarios = Array.isArray(dev?.scenarios) ? dev.scenarios : [];
    const scenarioIdOwners = new Map();
    scenarios.forEach((scen, scenIdx) => {
      const scenarioId = toSafeString(scen?.id).trim();
      if (!scenarioId) {
        pushValidationIssue(issues.errors, 'error', 'Scenario ID is required', {deviceIndex: devIdx, scenarioIndex: scenIdx});
      } else {
        if (!scenarioIdOwners.has(scenarioId)) {
          scenarioIdOwners.set(scenarioId, []);
        }
        scenarioIdOwners.get(scenarioId).push(scenIdx);
      }
    });

    scenarioIdOwners.forEach((owners, scenarioId) => {
      if (owners.length > 1) {
        owners.forEach((scenIdx) => {
          pushValidationIssue(
            issues.errors,
            'error',
            `Scenario ID "${scenarioId}" is duplicated inside this device`,
            {deviceIndex: devIdx, scenarioIndex: scenIdx}
          );
        });
      }
    });

    const scenarioIds = new Set(Array.from(scenarioIdOwners.keys()));
    const tplType = dev?.template?.type || '';
    if (tplType === 'on_mqtt_event') {
      const rules = Array.isArray(dev?.template?.mqtt?.rules) ? dev.template.mqtt.rules : [];
      rules.forEach((rule, idx) => {
        validateScenarioReference(issues, dev, devIdx, scenarioIds, rule?.scenario, `MQTT trigger ${idx + 1} scenario`, {templateField: 'mqtt-rule', templateIndex: idx});
      });
    } else if (tplType === 'on_flag') {
      const rules = Array.isArray(dev?.template?.flag?.rules) ? dev.template.flag.rules : [];
      rules.forEach((rule, idx) => {
        validateScenarioReference(issues, dev, devIdx, scenarioIds, rule?.scenario, `Flag trigger ${idx + 1} scenario`, {templateField: 'flag-rule', templateIndex: idx});
      });
    } else if (tplType === 'if_condition') {
      const condition = dev?.template?.condition || {};
      validateScenarioReference(issues, dev, devIdx, scenarioIds, condition.true_scenario, 'Condition TRUE branch scenario', {templateField: 'condition-scenario'});
      validateScenarioReference(issues, dev, devIdx, scenarioIds, condition.false_scenario, 'Condition FALSE branch scenario', {templateField: 'condition-scenario'});
    } else if (tplType === 'interval_task') {
      const interval = dev?.template?.interval || {};
      validateScenarioReference(issues, dev, devIdx, scenarioIds, interval.scenario, 'Interval action scenario', {templateField: 'interval'});
    } else if (tplType === 'sequence_lock') {
      const sequence = dev?.template?.sequence || {};
      validateScenarioReference(issues, dev, devIdx, scenarioIds, sequence.success_scenario, 'Sequence success scenario', {templateField: 'sequence'});
      validateScenarioReference(issues, dev, devIdx, scenarioIds, sequence.fail_scenario, 'Sequence failure scenario', {templateField: 'sequence'});
    }

    scenarios.forEach((scen, scenIdx) => {
      const steps = Array.isArray(scen?.steps) ? scen.steps : [];
      steps.forEach((step, stepIdx) => {
        const type = toSafeString(step?.type).trim() || 'nop';
        const stepLabel = stepTypeLabel(type);
        switch (type) {
          case 'mqtt_publish': {
            const topic = toSafeString(step?.data?.mqtt?.topic || step?.topic).trim();
            if (!topic) {
              pushValidationIssue(issues.errors, 'error', `${stepLabel} step requires topic`, {deviceIndex: devIdx, scenarioIndex: scenIdx, stepIndex: stepIdx});
            }
            break;
          }
          case 'audio_play': {
            const track = toSafeString(step?.data?.audio?.track || step?.track).trim();
            if (!track) {
              pushValidationIssue(issues.errors, 'error', `${stepLabel} step requires track`, {deviceIndex: devIdx, scenarioIndex: scenIdx, stepIndex: stepIdx});
            }
            break;
          }
          case 'set_flag': {
            const flag = toSafeString(step?.data?.flag?.flag || step?.flag).trim();
            if (!flag) {
              pushValidationIssue(issues.errors, 'error', `${stepLabel} step requires flag name`, {deviceIndex: devIdx, scenarioIndex: scenIdx, stepIndex: stepIdx});
            }
            break;
          }
          case 'event': {
            const eventName = toSafeString(step?.data?.event?.event || step?.event).trim();
            if (!eventName) {
              pushValidationIssue(issues.errors, 'error', `${stepLabel} step requires event name`, {deviceIndex: devIdx, scenarioIndex: scenIdx, stepIndex: stepIdx});
            }
            break;
          }
          case 'wait_flags': {
            const reqs = Array.isArray(step?.data?.wait_flags?.requirements) ? step.data.wait_flags.requirements : [];
            if (!reqs.length) {
              pushValidationIssue(issues.errors, 'error', `${stepLabel} step requires at least one requirement`, {deviceIndex: devIdx, scenarioIndex: scenIdx, stepIndex: stepIdx});
            }
            reqs.forEach((req, reqIdx) => {
              const flag = toSafeString(req?.flag).trim();
              if (!flag) {
                pushValidationIssue(issues.errors, 'error', `${stepLabel} requirement ${reqIdx + 1} requires flag name`, {deviceIndex: devIdx, scenarioIndex: scenIdx, stepIndex: stepIdx});
              }
            });
            break;
          }
          case 'loop': {
            const targetStep = toInt(step?.data?.loop?.target_step);
            if (targetStep < 0 || targetStep >= steps.length) {
              pushValidationIssue(issues.errors, 'error', `${stepLabel} step target step ${targetStep} is out of range`, {deviceIndex: devIdx, scenarioIndex: scenIdx, stepIndex: stepIdx});
            }
            break;
          }
          default:
            break;
        }
      });
    });
  });

  deviceIdOwners.forEach((owners, deviceId) => {
    if (owners.length > 1) {
      owners.forEach((devIdx) => {
        pushValidationIssue(issues.errors, 'error', `Device ID "${deviceId}" is duplicated`, {deviceIndex: devIdx});
      });
    }
  });

  return issues;
}

function serializeScenarioForSave(scen) {
  const next = Object.assign({}, scen);
  if (!Array.isArray(scen.steps)) {
    next.steps = [];
  } else {
    next.steps = scen.steps.map(serializeStepForSave);
  }
  return next;
}

function serializeStepForSave(step) {
  const safe = step || {};
  const out = {
    type: safe.type || 'nop',
    delay_ms: toInt(safe.delay_ms),
  };
  switch (out.type) {
    case 'mqtt_publish': {
      const mqtt = (safe.data && safe.data.mqtt) || {};
      out.topic = mqtt.topic || '';
      out.payload = mqtt.payload || '';
      out.qos = clamp(intOrDefault(mqtt.qos), 0, 2);
      out.retain = !!mqtt.retain;
      break;
    }
    case 'audio_play': {
      const audio = (safe.data && safe.data.audio) || {};
      out.track = audio.track || '';
      out.blocking = !!audio.blocking;
      break;
    }
    case 'set_flag': {
      const flag = (safe.data && safe.data.flag) || {};
      out.flag = flag.flag || '';
      out.value = !!flag.value;
      break;
    }
    case 'wait_flags': {
      const wf = (safe.data && safe.data.wait_flags) || {};
      const wait = {
        mode: wf.mode === 'any' ? 'any' : 'all',
        timeout_ms: toInt(wf.timeout_ms),
        requirements: [],
      };
      (wf.requirements || []).forEach(req => {
        if (!req) return;
        wait.requirements.push({
          flag: req.flag || '',
          state: req.required_state !== undefined ? !!req.required_state : !!req.state,
        });
      });
      out.wait = wait;
      break;
    }
    case 'loop': {
      const loop = (safe.data && safe.data.loop) || {};
      out.loop = {
        target_step: toInt(loop.target_step),
        max_iterations: toInt(loop.max_iterations),
      };
      break;
    }
    case 'event': {
      const event = (safe.data && safe.data.event) || {};
      out.event = event.event || '';
      out.topic = event.topic || '';
      out.payload = event.payload || '';
      break;
    }
    case 'audio_stop':
    case 'delay':
    case 'nop':
    default:
      break;
  }
  return out;
}

function stripTemplateRuntimeFields(dev) {
  if (!dev || !dev.template) {
    return;
  }
  if (dev.template.uid && Array.isArray(dev.template.uid.slots)) {
    dev.template.uid.slots.forEach((slot) => {
      if (slot && Object.prototype.hasOwnProperty.call(slot, 'last_value')) {
        delete slot.last_value;
      }
    });
  }
}

function toInt(value) {
  const n = parseInt(value, 10);
  return isNaN(n) ? 0 : n;
}

function intOrDefault(value, fallback) {
  const n = parseInt(value, 10);
  if (isNaN(n)) {
    return typeof fallback === 'number' ? fallback : 0;
  }
  return n;
}

function clamp(value, min, max) {
  return Math.max(min, Math.min(max, value));
}

function addDevice() {
  ensureModel();
  state.model.devices.push({
    id: `device_${Date.now().toString(16)}`,
    display_name: 'New device',
    scenarios: [],
  });
  state.selectedDevice = state.model.devices.length - 1;
  state.selectedScenario = -1;
  markDirty();
  renderAll();
}

function cloneDevice() {
  const dev = currentDevice();
  if (!dev) return;
  ensureModel();
  const copy = JSON.parse(JSON.stringify(dev));
  copy.id = (dev.id || 'device') + '_copy';
  copy.display_name = (dev.display_name || dev.id || 'Device') + ' copy';
  if (Object.prototype.hasOwnProperty.call(copy, 'name')) {
    delete copy.name;
  }
  state.model.devices.splice(state.selectedDevice + 1, 0, copy);
  state.selectedDevice += 1;
  state.selectedScenario = -1;
  markDirty();
  renderAll();
}

function deleteDevice() {
  ensureModel();
  if (state.selectedDevice < 0 || !state.model.devices.length) return;
  state.model.devices.splice(state.selectedDevice, 1);
  if (!state.model.devices.length) {
    state.selectedDevice = -1;
    state.selectedScenario = -1;
  } else {
    state.selectedDevice = Math.min(state.selectedDevice, state.model.devices.length - 1);
    state.selectedScenario = -1;
  }
  markDirty();
  renderAll();
}

function addScenario() {
  const dev = currentDevice();
  if (!dev) return;
  dev.scenarios = dev.scenarios || [];
  dev.scenarios.push({id: `scenario_${Date.now().toString(16)}`, name: 'Scenario', steps: []});
  state.selectedScenario = dev.scenarios.length - 1;
  markDirty();
  renderDeviceDetail();
}

function addScenarioForReference(field, subfield, indexStr) {
  const dev = currentDevice();
  if (!dev) return;
  dev.scenarios = dev.scenarios || [];
  const scenario = {
    id: `scenario_${Date.now().toString(16)}`,
    name: 'Scenario',
    steps: [],
  };
  dev.scenarios.push(scenario);
  state.selectedScenario = dev.scenarios.length - 1;
  setTemplateScenarioReference(dev, field, subfield, indexStr, scenario.id);
  markDirty();
  renderDeviceDetail();
}

function removeScenario(indexStr) {
  const dev = currentDevice();
  if (!dev || !dev.scenarios || !dev.scenarios.length) return;
  const idx = parseInt(indexStr, 10);
  const target = isNaN(idx) ? state.selectedScenario : idx;
  if (target < 0) return;
  dev.scenarios.splice(target, 1);
  state.selectedScenario = Math.min(target, (dev.scenarios.length - 1));
  markDirty();
  renderDeviceDetail();
}

function selectScenario(indexStr) {
  const idx = parseInt(indexStr, 10);
  if (isNaN(idx)) return;
  state.selectedScenario = idx;
  renderDeviceDetail();
}

function addStep(type) {
  const scen = currentScenario();
  if (!scen) return;
  scen.steps = scen.steps || [];
  const stepType = ACTION_TYPES.includes(type) ? type : 'mqtt_publish';
  const step = {type: stepType, delay_ms: 0, data: {}};
  normalizeStepForEditing(step);
  scen.steps.push(step);
  markDirty();
  renderDeviceDetail();
}

function removeStep(indexStr) {
  const idx = parseInt(indexStr, 10);
  const scen = currentScenario();
  if (!scen || isNaN(idx) || !scen.steps) return;
  scen.steps.splice(idx, 1);
  markDirty();
  renderDeviceDetail();
}

function moveStep(indexStr, delta) {
  const idx = parseInt(indexStr, 10);
  const scen = currentScenario();
  if (!scen || isNaN(idx) || !scen.steps || !scen.steps[idx]) return;
  const target = idx + delta;
  if (target < 0 || target >= scen.steps.length) return;
  const [step] = scen.steps.splice(idx, 1);
  scen.steps.splice(target, 0, step);
  markDirty();
  renderDeviceDetail();
}

function updateValidationState() {
  state.validation = collectValidationIssues(state.model);
  return state.validation;
}

function formatValidationIssue(issue) {
  if (!issue) return '';
  const parts = [];
  if (typeof issue.deviceIndex === 'number' && issue.deviceIndex >= 0) {
    const dev = state.model?.devices?.[issue.deviceIndex];
    const devName = toSafeString(dev?.display_name || dev?.id).trim() || `Device ${issue.deviceIndex + 1}`;
    parts.push(devName);
  }
  if (typeof issue.scenarioIndex === 'number' && issue.scenarioIndex >= 0 && typeof issue.deviceIndex === 'number' && issue.deviceIndex >= 0) {
    const scen = state.model?.devices?.[issue.deviceIndex]?.scenarios?.[issue.scenarioIndex];
    const scenarioName = toSafeString(scen?.name || scen?.id).trim() || `Scenario ${issue.scenarioIndex + 1}`;
    parts.push(scenarioName);
  }
  if (typeof issue.stepIndex === 'number' && issue.stepIndex >= 0) {
    parts.push(`Step ${issue.stepIndex + 1}`);
  }
  const prefix = parts.length ? `${parts.join(' / ')}: ` : '';
  return prefix + issue.message;
}

function renderValidationOverview() {
  if (!state.validationEl) return;
  const validation = state.validation || {errors: [], warnings: []};
  const errors = validation.errors || [];
  const warnings = validation.warnings || [];
  const total = errors.length + warnings.length;
  if (!total) {
    state.validationEl.classList.add('hidden');
    state.validationEl.innerHTML = '';
    return;
  }
  const items = []
    .concat(errors.map((issue) => `<li class="dw-validation-item error">${escapeHtml(formatValidationIssue(issue))}</li>`))
    .concat(warnings.map((issue) => `<li class="dw-validation-item warning">${escapeHtml(formatValidationIssue(issue))}</li>`));
  state.validationEl.classList.remove('hidden');
  state.validationEl.innerHTML = `
    <div class="dw-validation-head">
      <span class="dw-validation-title">Preflight validation</span>
      <span class="dw-validation-badges">
        <span class="dw-validation-badge error">${errors.length} errors</span>
        <span class="dw-validation-badge warning">${warnings.length} warnings</span>
      </span>
    </div>
    <ul class="dw-validation-list">${items.join('')}</ul>`;
}

function renderJson() {
  if (!state.json) return;
  if (!state.model) {
    state.json.textContent = 'No config loaded';
    return;
  }
  state.json.textContent = JSON.stringify(state.model, null, 2);
  if (state.jsonWrap) {
    state.jsonWrap.classList.toggle('collapsed', !state.jsonVisible);
  }
}

function updateToolbar() {
  const saveBtn = state.toolbar?.querySelector('[data-action="save"]');
  const errorCount = (state.validation?.errors || []).length;
  if (saveBtn) {
    saveBtn.disabled = !state.dirty || state.busy || errorCount > 0;
    saveBtn.title = errorCount > 0 ? `Fix ${errorCount} validation errors before saving` : '';
  }
  const cloneBtn = state.toolbar?.querySelector('[data-action="clone-device"]');
  if (cloneBtn) cloneBtn.disabled = state.selectedDevice < 0;
  const deleteBtn = state.toolbar?.querySelector('[data-action="delete-device"]');
  if (deleteBtn) deleteBtn.disabled = state.selectedDevice < 0;
}

function updateGlobals() {
  const schema = document.getElementById('dw_schema');
  if (schema && state.model) schema.value = state.model.schema ?? state.model.schema_version ?? 1;
  window.__devicesWizardRenderActions = renderActions;
}

function toSafeString(value) {
  if (value === null || value === undefined) {
    return '';
  }
  return String(value);
}

function escapeHtml(text) {
  const str = toSafeString(text);
  return str.replace(/[&<>"']/g, (c) => ({
    '&': '&amp;',
    '<': '&lt;',
    '>': '&gt;',
    '"': '&quot;',
    "'": '&#39;',
  }[c] || c));
}

function escapeAttr(text) {
  return escapeHtml(text);
}

function setStatus(text, color) {
  if (!state.statusEl) return;
  state.statusEl.textContent = text;
  if (color) state.statusEl.style.color = color;
}

function renderProfiles() {
  if (!state.profileList) return;
  const profiles = Array.isArray(state.profiles) ? state.profiles : [];
  if (!profiles.length) {
    state.profileList.innerHTML = "<div class='dw-empty small'>No profiles</div>";
    return;
  }
  state.profileList.innerHTML = profiles.map((profile) => {
    const active = profile.id === state.activeProfile ? ' active' : '';
    const label = escapeHtml(profile.name || profile.id);
    return `<div class="dw-profile-chip${active}" data-profile-id="${escapeAttr(profile.id)}">${label}</div>`;
  }).join('');
}

function renderActions() {
  if (!state.actionsRoot) return;
  const signalMonitorList = Array.isArray(window.__BROKER_STATUS?.signal_monitor) ? window.__BROKER_STATUS.signal_monitor : [];
  const signalMonitorMap = new Map(signalMonitorList
    .filter((item) => item && item.id)
    .map((item) => [item.id, item]));
  const sequenceMonitorList = Array.isArray(window.__BROKER_STATUS?.sequence_monitor) ? window.__BROKER_STATUS.sequence_monitor : [];
  const sequenceMonitorMap = new Map(sequenceMonitorList
    .filter((item) => item && item.id)
    .map((item) => [item.id, item]));
  const devices = state.model?.devices || [];
  const groups = devices.map((dev) => {
    const scenarios = (dev.scenarios || []).filter(sc => sc && sc.button_enabled && sc.id);
    const signalState = signalMonitorMap.get(dev.id);
    const sequenceState = sequenceMonitorMap.get(dev.id);
    if ((!scenarios.length && !signalState && !sequenceState) || !dev.id) {
      return '';
    }
    const title = escapeHtml(dev.display_name || dev.id);
    const buttons = scenarios.map((sc) => {
      const label = escapeHtml(sc.button_label || sc.name || sc.id);
      return `<button class="dw-action-btn" data-run-device="${escapeAttr(dev.id)}" data-run-scenario="${escapeAttr(sc.id)}">${label}</button>`;
    }).join('');
    const signalBlock = signalState ? renderActionSignalState(signalState) : '';
    const sequenceBlock = sequenceState ? renderActionSequenceState(sequenceState) : '';
    return `<div class="dw-action-group">
      <div class="dw-action-title">${title}</div>
      ${buttons ? `<div class="dw-action-buttons">${buttons}</div>` : ''}
      ${signalBlock}
      ${sequenceBlock}
    </div>`;
  }).filter(Boolean).join('');
  state.actionsRoot.innerHTML = groups || "<div class='dw-empty'>No action buttons or live device widgets configured.</div>";
}

function renderActionSignalState(dev) {
  const stateLabel = escapeHtml(dev.state || 'idle');
  const progressMs = Number(dev.progress_ms || 0);
  const requiredMs = Number(dev.required_hold_ms || 0);
  const progress = `${progressMs} / ${requiredMs} ms`;
  const progressPct = requiredMs > 0 ? Math.max(0, Math.min(100, Math.round((progressMs / requiredMs) * 100))) : 0;
  const heartbeatTopic = dev.heartbeat_topic ? escapeHtml(dev.heartbeat_topic) : '&mdash;';
  const timeout = dev.heartbeat_timeout_ms ? `${dev.time_left_ms || 0} ms to timeout` : 'No timeout';
  const resetTopic = dev.reset_topic ? escapeHtml(dev.reset_topic) : '&mdash;';
  const stateKey = escapeAttr(dev.state || 'idle');
  const deviceId = escapeAttr(dev.id || dev.device_id || '');
  return `<div class="signal-device" data-signal-state="${stateKey}">
    <div class="signal-meta">
      <span class="signal-pill">Signal: ${stateLabel}</span>
      <span class="signal-pill">Progress: ${progress}</span>
      <span class="signal-pill">${timeout}</span>
      <button class="secondary small signal-reset-btn" data-signal-reset-device="${deviceId}">Reset</button>
    </div>
    <div class="signal-progress"><div class="signal-progress-bar" style="width:${progressPct}%"></div></div>
    <div class="signal-details">
      <div class="signal-detail signal-expected"><span class="label">Heartbeat topic</span><strong>${heartbeatTopic}</strong></div>
      <div class="signal-detail"><span class="label">Reset topic</span><strong>${resetTopic}</strong></div>
      <div class="signal-detail"><span class="label">Required hold</span><strong>${requiredMs} ms</strong></div>
    </div>
  </div>`;
}

function renderActionSequenceState(dev) {
  const stateLabel = escapeHtml(dev.state || 'idle');
  const completed = Number(dev.completed_steps || 0);
  const total = Number(dev.total_steps || 0);
  const progress = `${completed} / ${total}`;
  const progressPct = total > 0 ? Math.max(0, Math.min(100, Math.round((completed / total) * 100))) : 0;
  const nextTopic = dev.expected_topic ? escapeHtml(dev.expected_topic) : '&mdash;';
  const nextPayload = dev.payload_required
    ? (dev.expected_payload ? escapeHtml(dev.expected_payload) : '<em>required</em>')
    : 'Any payload';
  const timeout = dev.timeout_ms ? `${dev.time_left_ms || 0} ms left` : 'No timeout';
  const stateKey = escapeAttr(dev.state || 'idle');
  const deviceId = escapeAttr(dev.id || dev.device_id || '');
  return `<div class="sequence-device" data-sequence-state="${stateKey}">
    <div class="sequence-meta">
      <span class="sequence-pill">Sequence: ${stateLabel}</span>
      <span class="sequence-pill">Progress: ${progress}</span>
      <span class="sequence-pill">${timeout}</span>
      <button class="secondary small sequence-reset-btn" data-sequence-reset-device="${deviceId}">Reset</button>
    </div>
    <div class="sequence-progress"><div class="sequence-progress-bar" style="width:${progressPct}%"></div></div>
    <div class="sequence-details">
      <div class="sequence-detail sequence-expected"><span class="label">Expected topic</span><strong>${nextTopic}</strong></div>
      <div class="sequence-detail"><span class="label">Expected payload</span><strong>${nextPayload}</strong></div>
      <div class="sequence-detail"><span class="label">Reset on wrong step</span><strong>${dev.reset_on_error ? 'Yes' : 'No'}</strong></div>
    </div>
  </div>`;
}

function createProfile(cloneId) {
  const id = prompt('Profile id:', 'profile_' + Date.now().toString(16));
  if (!id) {
    return;
  }
  const name = prompt('Display name:', id) || id;
  let url = '/api/devices/profile/create?id=' + encodeURIComponent(id) +
    '&name=' + encodeURIComponent(name);
  if (cloneId) {
    url += '&clone=' + encodeURIComponent(cloneId);
  }
  fetch(url, {method: 'POST'})
    .then(() => loadModel())
    .catch(err => setStatus('Profile create failed: ' + err.message, '#f87171'));
}

function cloneProfile() {
  if (!state.activeProfile) {
    return;
  }
  createProfile(state.activeProfile);
}

function downloadProfile() {
  const id = state.activeProfile || (state.profiles[0]?.id) || '';
  const url = id ? `/api/devices/profile/download?profile=${encodeURIComponent(id)}` : '/api/devices/profile/download';
  const anchor = document.createElement('a');
  anchor.href = url;
  anchor.style.display = 'none';
  document.body.appendChild(anchor);
  anchor.click();
  setTimeout(() => {
    document.body.removeChild(anchor);
  }, 0);
}

function renameProfile() {
  if (!state.activeProfile) {
    return;
  }
  const name = prompt('New profile name:', state.activeProfile);
  if (!name) {
    return;
  }
  fetch('/api/devices/profile/rename?id=' + encodeURIComponent(state.activeProfile) +
    '&name=' + encodeURIComponent(name), {method: 'POST'})
    .then(() => loadModel())
    .catch(err => setStatus('Rename failed: ' + err.message, '#f87171'));
}

function deleteProfile() {
  if (!state.activeProfile) {
    return;
  }
  if (!confirm('Delete profile ' + state.activeProfile + '?')) {
    return;
  }
  fetch('/api/devices/profile/delete?id=' + encodeURIComponent(state.activeProfile), {method: 'POST'})
    .then(() => loadModel())
    .catch(err => setStatus('Delete failed: ' + err.message, '#f87171'));
}

function activateProfile(id) {
  if (!id || id === state.activeProfile) {
    return;
  }
  state.activeProfile = id;
  renderProfiles();
  fetch('/api/devices/profile/activate?id=' + encodeURIComponent(id), {method: 'POST'})
    .then(() => loadModel())
    .catch(err => setStatus('Activate failed: ' + err.message, '#f87171'));
}

function injectWizardStyles() {
  if (document.getElementById('dw_wizard_styles')) return;
  const style = document.createElement('style');
  style.id = 'dw_wizard_styles';
  style.textContent = `
    .dw-modal{position:fixed;inset:0;display:flex;align-items:center;justify-content:center;background:rgba(15,23,42,0.6);z-index:50;}
    .dw-modal.hidden{display:none;}
    .dw-modal .dw-modal-content{background:#0f172a;color:#e2e8f0;border:1px solid #1f2937;border-radius:12px;max-width:720px;width:90%;max-height:90vh;overflow:auto;box-shadow:0 25px 50px -12px rgba(0,0,0,.5);}
    .dw-wizard{padding:24px;display:flex;flex-direction:column;gap:16px;}
    .dw-wizard h3{margin:0;font-size:1.3rem;}
    .dw-wizard-body{display:flex;flex-direction:column;gap:16px;}
    .dw-wizard-fields .dw-field{margin-bottom:12px;}
    .dw-wizard-templates{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:12px;}
    .dw-wizard-template{border:1px solid #1f2937;border-radius:8px;padding:12px;cursor:pointer;background:#111827;transition:all .2s;}
    .dw-wizard-template.selected{border-color:#38bdf8;background:#0d253b;}
    .dw-wizard-summary{background:#0b1120;border:1px solid #1f2937;border-radius:8px;padding:12px;font-family:monospace;font-size:0.85rem;max-height:280px;overflow:auto;}
    .dw-wizard-nav{display:flex;justify-content:space-between;align-items:center;}
    .dw-wizard-nav .right{display:flex;gap:8px;}
    .dw-wizard-note{font-size:0.85rem;color:#94a3b8;}
    .dw-inline-actions{display:flex;gap:8px;align-items:center;flex-wrap:wrap;}
    .dw-inline-actions select{flex:1 1 220px;min-width:180px;}
    .dw-inline-actions button{flex:0 0 auto;}
    .dw-scenario-group{display:flex;flex-direction:column;gap:6px;margin-bottom:14px;}
    .dw-scenario-group:last-child{margin-bottom:0;}
    .dw-scenario-group-title{font-size:12px;font-weight:700;letter-spacing:.04em;text-transform:uppercase;color:#94a3b8;}
    .dw-scenario-main{display:flex;flex-direction:column;gap:6px;min-width:0;}
    .dw-chip-row{display:flex;gap:6px;flex-wrap:wrap;}
    .dw-chip{display:inline-flex;align-items:center;padding:3px 8px;border-radius:999px;font-size:11px;font-weight:700;letter-spacing:.02em;}
    .dw-chip.required{background:rgba(14,165,233,0.15);border:1px solid rgba(56,189,248,0.35);color:#bae6fd;}
    .dw-chip.linked{background:rgba(59,130,246,0.15);border:1px solid rgba(96,165,250,0.35);color:#bfdbfe;}
    .dw-chip.manual{background:rgba(148,163,184,0.12);border:1px solid rgba(148,163,184,0.28);color:#cbd5e1;}
    .dw-usage-list{display:flex;flex-direction:column;gap:6px;}
    .dw-usage-item{padding:8px 10px;border-radius:10px;border:1px solid rgba(71,85,105,0.7);background:rgba(15,23,42,0.42);color:#dbe4f0;font-size:13px;}
    .dw-track-field{display:flex;gap:8px;align-items:center;flex-wrap:wrap;}
    .dw-track-field input{flex:1 1 260px;min-width:180px;}
    .dw-step-title-wrap{display:flex;flex-direction:column;gap:2px;}
    .dw-step-summary{font-size:12px;line-height:1.4;color:#cbd5e1;max-width:42rem;}
    .dw-step-index{font-size:11px;font-weight:700;letter-spacing:.04em;text-transform:uppercase;color:#94a3b8;}
    .dw-track-picker{padding:20px;display:flex;flex-direction:column;gap:14px;}
    .dw-track-picker-head{display:flex;justify-content:space-between;align-items:center;gap:12px;}
    .dw-track-picker-current{display:flex;flex-direction:column;gap:4px;padding:10px 12px;border:1px solid rgba(51,65,85,0.85);border-radius:10px;background:rgba(15,23,42,0.4);}
    .dw-track-picker-current-label{font-size:11px;font-weight:700;letter-spacing:.05em;text-transform:uppercase;color:#94a3b8;}
    .dw-track-picker-current-value{font-size:13px;color:#e2e8f0;word-break:break-all;}
    .dw-track-picker-results{display:flex;flex-direction:column;gap:12px;max-height:58vh;overflow:auto;padding-right:4px;}
    .dw-track-picker-group{display:flex;flex-direction:column;gap:8px;}
    .dw-track-picker-group-title{font-size:12px;font-weight:700;color:#cbd5e1;}
    .dw-track-picker-items{display:flex;flex-direction:column;gap:8px;}
    .dw-track-picker-item{display:flex;flex-direction:column;align-items:flex-start;gap:3px;width:100%;padding:10px 12px;border-radius:10px;border:1px solid rgba(71,85,105,0.7);background:rgba(15,23,42,0.45);color:#e2e8f0;cursor:pointer;transition:border-color .15s ease,background .15s ease,transform .15s ease;}
    .dw-track-picker-item:hover{border-color:rgba(96,165,250,0.62);background:rgba(30,41,59,0.85);transform:translateY(-1px);}
    .dw-track-picker-item.active{border-color:rgba(34,197,94,0.65);background:rgba(21,128,61,0.16);}
    .dw-track-picker-name{font-size:13px;font-weight:700;color:#f8fafc;}
    .dw-track-picker-path{font-size:11px;color:#94a3b8;word-break:break-all;text-align:left;}
    .dw-step-preset-panel{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:10px;width:100%;}
    .dw-step-preset-group{border:1px solid rgba(51,65,85,0.85);border-radius:10px;padding:10px;background:rgba(15,23,42,0.45);display:flex;flex-direction:column;gap:8px;}
    .dw-step-preset-title{font-size:11px;font-weight:700;letter-spacing:.05em;text-transform:uppercase;color:#94a3b8;}
    .dw-step-presets{display:flex;gap:8px;flex-wrap:wrap;}
    .dw-step-presets button{margin:0;}
    .dw-step-preset-btn{display:flex;flex-direction:column;align-items:flex-start;gap:4px;min-width:150px;padding:10px 12px;border:1px solid rgba(71,85,105,0.9);border-radius:10px;background:linear-gradient(180deg,rgba(15,23,42,0.9),rgba(15,23,42,0.72));color:#e2e8f0;cursor:pointer;transition:border-color .15s ease,transform .15s ease,background .15s ease,box-shadow .15s ease;}
    .dw-step-preset-btn:hover{border-color:rgba(96,165,250,0.7);background:linear-gradient(180deg,rgba(15,23,42,1),rgba(30,41,59,0.88));transform:translateY(-1px);box-shadow:0 10px 22px rgba(2,6,23,0.28);}
    .dw-step-preset-label{font-size:13px;font-weight:700;color:#f8fafc;}
    .dw-step-preset-desc{font-size:11px;line-height:1.35;color:#94a3b8;text-align:left;}
    .dw-wait-panel{border:1px solid rgba(51,65,85,0.85);border-radius:12px;padding:12px;background:rgba(15,23,42,0.38);display:flex;flex-direction:column;gap:12px;}
    .dw-wait-panel-head{display:flex;flex-direction:column;gap:8px;}
    .dw-wait-headline{font-size:14px;font-weight:700;color:#e2e8f0;}
    .dw-wait-summary{display:flex;gap:6px;flex-wrap:wrap;}
    .dw-wait-chip{display:inline-flex;align-items:center;padding:4px 8px;border-radius:999px;background:rgba(96,165,250,0.14);border:1px solid rgba(96,165,250,0.28);color:#dbeafe;font-size:12px;font-weight:600;}
    .dw-wait-settings{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:10px;}
    .dw-known-flags{display:flex;flex-direction:column;gap:8px;}
    .dw-known-flags-title{font-size:11px;font-weight:700;letter-spacing:.05em;text-transform:uppercase;color:#94a3b8;}
    .dw-known-flags-list{display:flex;gap:8px;flex-wrap:wrap;}
    .dw-flag-chip-btn{display:inline-flex;align-items:center;padding:6px 10px;border-radius:999px;border:1px solid rgba(96,165,250,0.28);background:rgba(59,130,246,0.12);color:#dbeafe;font-size:12px;font-weight:600;cursor:pointer;transition:border-color .15s ease,background .15s ease,transform .15s ease;}
    .dw-flag-chip-btn:hover{border-color:rgba(96,165,250,0.6);background:rgba(59,130,246,0.2);transform:translateY(-1px);}
    .dw-wait-card{border:1px solid rgba(71,85,105,0.75);border-radius:10px;padding:10px;background:rgba(15,23,42,0.52);display:flex;flex-direction:column;gap:10px;}
    .dw-wait-card-head{display:flex;justify-content:space-between;align-items:center;gap:8px;}
    .dw-wait-card-title{font-size:12px;font-weight:700;color:#e2e8f0;}
    .dw-wait-row{display:grid;grid-template-columns:minmax(140px,1fr) 120px minmax(140px,1fr);gap:8px;align-items:center;}
    .dw-wait-preview{font-size:12px;color:#94a3b8;}
    @media(max-width:900px){.dw-wait-row{grid-template-columns:1fr;}.dw-wait-preview{padding-top:2px;}}
    .dw-validation{margin:10px 0 14px;padding:12px 14px;border:1px solid #334155;border-radius:12px;background:#0f172a;}
    .dw-validation.hidden{display:none;}
    .dw-validation-head{display:flex;justify-content:space-between;align-items:center;gap:12px;margin-bottom:8px;flex-wrap:wrap;}
    .dw-validation-title{font-weight:600;color:#e2e8f0;}
    .dw-validation-badges{display:flex;gap:8px;flex-wrap:wrap;}
    .dw-validation-badge{display:inline-flex;align-items:center;padding:4px 8px;border-radius:999px;font-size:12px;font-weight:600;}
    .dw-validation-badge.error{background:rgba(239,68,68,0.16);color:#fecaca;border:1px solid rgba(239,68,68,0.3);}
    .dw-validation-badge.warning{background:rgba(245,158,11,0.16);color:#fde68a;border:1px solid rgba(245,158,11,0.3);}
    .dw-validation-list{margin:0;padding-left:18px;display:flex;flex-direction:column;gap:6px;}
    .dw-validation-item{font-size:13px;line-height:1.45;}
    .dw-validation-item.error{color:#fecaca;}
    .dw-validation-item.warning{color:#fde68a;}
  `;
  document.head.appendChild(style);
}

function runScenario(deviceId, scenarioId) {
  const devId = (toSafeString(deviceId).trim()) || (toSafeString(currentDevice()?.id).trim());
  const scenId = (toSafeString(scenarioId).trim()) || (toSafeString(currentScenario()?.id).trim());
  if (!devId || !scenId) {
    setStatus('Select a scenario to run', '#f87171');
    return;
  }
  const url = `/api/devices/run?device=${encodeURIComponent(devId)}&scenario=${encodeURIComponent(scenId)}`;
  setStatus('Triggering scenario...', '#fbbf24');
  fetch(url)
    .then(r => {
      if (!r.ok) throw new Error('HTTP ' + r.status);
      return r.json().catch(() => ({}));
    })
    .then(() => {
      setStatus('Scenario queued', '#22c55e');
    })
    .catch(err => {
      setStatus('Run failed: ' + err.message, '#f87171');
    });
}

function resetSequence(deviceId, btn) {
  const devId = toSafeString(deviceId).trim();
  if (!devId) {
    setStatus('Sequence reset failed: missing device id', '#f87171');
    return;
  }
  if (btn) {
    btn.disabled = true;
  }
  setStatus('Resetting sequence...', '#fbbf24');
  fetch(`/api/devices/sequence/reset?device=${encodeURIComponent(devId)}`, {method: 'POST'})
    .then(async (r) => {
      const text = await r.text().catch(() => '');
      if (!r.ok) {
        throw new Error((text || '').trim() || ('HTTP ' + r.status));
      }
      return text;
    })
    .then(() => {
      setStatus('Sequence reset', '#22c55e');
      return loadStatus().catch(() => {});
    })
    .catch((err) => {
      setStatus('Sequence reset failed: ' + err.message, '#f87171');
    })
    .finally(() => {
      if (btn) {
        btn.disabled = false;
      }
    });
}

function resetSignal(deviceId, btn) {
  const devId = toSafeString(deviceId).trim();
  if (!devId) {
    setStatus('Signal reset failed: missing device id', '#f87171');
    return;
  }
  if (btn) {
    btn.disabled = true;
  }
  setStatus('Resetting signal hold...', '#fbbf24');
  fetch(`/api/devices/signal/reset?device=${encodeURIComponent(devId)}`, {method: 'POST'})
    .then(async (r) => {
      const text = await r.text().catch(() => '');
      if (!r.ok) {
        throw new Error((text || '').trim() || ('HTTP ' + r.status));
      }
      return text;
    })
    .then(() => {
      setStatus('Signal hold reset', '#22c55e');
      return loadStatus().catch(() => {});
    })
    .catch((err) => {
      setStatus('Signal reset failed: ' + err.message, '#f87171');
    })
    .finally(() => {
      if (btn) {
        btn.disabled = false;
      }
    });
}
function openWizard(templateId) {
  state.wizard.open = true;
  state.wizard.step = 0;
  state.wizard.autoId = true;
  state.wizard.data = {deviceName: '', deviceId: ''};
  state.wizard.template = null;
  if (templateId && WIZARD_TEMPLATES[templateId]) {
    applyWizardTemplate(templateId);
    state.wizard.step = 1;
  }
  renderWizard();
}

function closeWizard() {
  state.wizard.open = false;
  state.wizard.step = 0;
  state.wizard.template = null;
  state.wizard.data = {};
  state.wizardModal?.classList.add('hidden');
}

function applyWizardTemplate(templateId) {
  const tpl = WIZARD_TEMPLATES[templateId];
  if (!tpl) {
    state.wizard.template = null;
    return;
  }
  state.wizard.template = templateId;
  state.wizard.data = {deviceName: tpl.defaults?.deviceName || '', deviceId: ''};
  state.wizard.autoId = true;
  if (tpl.defaults) {
    Object.keys(tpl.defaults).forEach((key) => {
      state.wizard.data[key] = tpl.defaults[key];
    });
  }
  state.wizard.data.deviceId = slugify(state.wizard.data.deviceName || templateId);
}

function renderWizard() {
  if (!state.wizardModal || !state.wizardContent) return;
  if (!state.wizard.open) {
    state.wizardModal.classList.add('hidden');
    state.wizardContent.innerHTML = '';
    return;
  }
  state.wizardModal.classList.remove('hidden');
  const stepTitle = getWizardStepTitle(state.wizard.step);
  const total = wizardTotalSteps();
  const isLast = state.wizard.step >= total - 1;
  const nextLabel = isLast ? 'Create device' : 'Next';
  const nextAction = isLast ? 'apply' : 'next';
  const nextDisabled = wizardForwardDisabled();
  const body = renderWizardStepBody();
  state.wizardContent.innerHTML = `
    <div class="dw-wizard">
      <div>
        <h3>${stepTitle}</h3>
        <div class="dw-wizard-note">${getWizardStepNote(state.wizard.step)}</div>
      </div>
      <div class="dw-wizard-body">
        ${body}
      </div>
      <div class="dw-wizard-nav">
        <button data-wizard-action="close">Cancel</button>
        <div class="right">
          <button data-wizard-action="prev" ${state.wizard.step === 0 ? 'disabled' : ''}>Back</button>
          <button data-wizard-action="${nextAction}" ${nextDisabled ? 'disabled' : ''}>${nextLabel}</button>
        </div>
      </div>
    </div>`;
  if (state.wizard.step === total - 1) {
    updateWizardSummary();
  }
}

function renderWizardStepBody() {
  switch (state.wizard.step) {
    case 0:
      return renderWizardTemplateSelection();
    case 1:
      return renderWizardBasics();
    case 2:
      return renderWizardTemplateFields();
    case 3:
      return `<div class="dw-wizard-summary" id="dw_wizard_summary"></div>`;
    default:
      return '';
  }
}

function renderWizardTemplateSelection() {
  const cards = Object.entries(WIZARD_TEMPLATES).map(([id, tpl]) => {
    const active = state.wizard.template === id ? ' selected' : '';
    return `<div class="dw-wizard-template${active}" data-wizard-action="select-template" data-template-id="${id}">
      <strong>${escapeHtml(tpl.label)}</strong>
      <p class="dw-wizard-note">${escapeHtml(tpl.description)}</p>
    </div>`;
  }).join('');
  return `<div class="dw-wizard-templates">${cards}</div>`;
}

function renderWizardBasics() {
  return `
    <div class="dw-wizard-fields">
      <div class="dw-field">
        <label>Device name</label>
        <input data-wizard-field="deviceName" value="${escapeAttr(state.wizard.data.deviceName || '')}" placeholder="Friendly name">
      </div>
      <div class="dw-field">
        <label>Device ID</label>
        <input data-wizard-field="deviceId" value="${escapeAttr(state.wizard.data.deviceId || '')}" placeholder="unique_id">
        <div class="dw-wizard-note">Used internally &mdash; only letters/numbers.</div>
      </div>
    </div>`;
}

function renderWizardTemplateFields() {
  if (!state.wizard.template) {
    return `<div class="dw-wizard-note">Select a template first.</div>`;
  }
  const tpl = WIZARD_TEMPLATES[state.wizard.template];
  if (!tpl?.fields?.length) {
    return `<div class="dw-wizard-note">No additional settings for this template.</div>`;
  }
  const fields = tpl.fields.map((field) => `
    <div class="dw-field">
      <label>${escapeHtml(field.label)}</label>
      <input data-wizard-field="${field.name}" value="${escapeAttr(state.wizard.data[field.name] || '')}" placeholder="${escapeAttr(field.placeholder || '')}">
    </div>`).join('');
  return `<div class="dw-wizard-fields">${fields}</div>`;
}

function updateWizardSummary() {
  const summaryEl = document.getElementById('dw_wizard_summary');
  if (!summaryEl) return;
  const device = buildDeviceFromWizard();
  if (!device) {
    summaryEl.textContent = 'Select template and fill required fields.';
    return;
  }
  summaryEl.textContent = JSON.stringify(device, null, 2);
}

function getWizardStepTitle(step) {
  switch (step) {
    case 0: return 'Choose a template';
    case 1: return 'Basics';
    case 2: return 'Template settings';
    case 3: return 'Summary';
    default: return 'Wizard';
  }
}

function getWizardStepNote(step) {
  switch (step) {
    case 0: return 'Pick a ready-to-go configuration.';
    case 1: return 'Set the device name and identifier.';
    case 2: return 'Customize actions for this template.';
    case 3: return 'Review the generated device before adding.';
    default: return '';
  }
}

function wizardTotalSteps() {
  return 4;
}

function wizardForwardDisabled() {
  if (state.wizard.step === 0) {
    return !state.wizard.template;
  }
  if (state.wizard.step === 1) {
    return !(state.wizard.data.deviceName && state.wizard.data.deviceId);
  }
  return false;
}

function handleWizardClick(ev) {
  if (!state.wizard.open) return;
  if (ev.target === state.wizardModal) {
    closeWizard();
    return;
  }
  const btn = ev.target.closest('[data-wizard-action]');
  if (!btn) return;
  const action = btn.dataset.wizardAction;
  switch (action) {
    case 'close':
      closeWizard();
      break;
    case 'next':
      wizardNext();
      break;
    case 'prev':
      wizardPrev();
      break;
    case 'apply':
      wizardApply();
      break;
    case 'select-template':
      selectWizardTemplate(btn.dataset.templateId);
      break;
    default:
      break;
  }
}

function handleWizardInput(ev) {
  if (!state.wizard.open) return;
  const target = ev.target;
  if (!target?.dataset?.wizardField) return;
  const field = target.dataset.wizardField;
  state.wizard.data[field] = target.value;
  if (field === 'deviceName' && state.wizard.autoId) {
    const slug = slugify(target.value);
    state.wizard.data.deviceId = slug;
    const idInput = state.wizardModal?.querySelector('input[data-wizard-field="deviceId"]');
    if (idInput) idInput.value = slug;
  }
  if (field === 'deviceId') {
    state.wizard.autoId = false;
  }
  if (state.wizard.step === wizardTotalSteps() - 1) {
    updateWizardSummary();
  }
}

function selectWizardTemplate(templateId) {
  if (!templateId || !WIZARD_TEMPLATES[templateId]) {
    return;
  }
  applyWizardTemplate(templateId);
  state.wizard.step = 1;
  renderWizard();
}

function wizardNext() {
  if (wizardForwardDisabled()) {
    return;
  }
  const total = wizardTotalSteps();
  state.wizard.step = Math.min(state.wizard.step + 1, total - 1);
  renderWizard();
}

function wizardPrev() {
  state.wizard.step = Math.max(state.wizard.step - 1, 0);
  renderWizard();
}

function wizardApply() {
  const device = buildDeviceFromWizard();
  if (!device) {
    return;
  }
  ensureModel();
  state.model.devices.push(device);
  state.selectedDevice = state.model.devices.length - 1;
  state.selectedScenario = -1;
  closeWizard();
  renderAll();
  markDirty();
  setStatus('Device added via wizard', '#22c55e');
}

function buildDeviceFromWizard() {
  const tpl = WIZARD_TEMPLATES[state.wizard.template];
  if (!tpl) {
    return null;
  }
  const name = (state.wizard.data.deviceName || tpl.defaults?.deviceName || 'Device').trim();
  const id = (state.wizard.data.deviceId || slugify(name)).trim();
  const base = {
    id: id || slugify(name),
    display_name: name || 'Device',
    scenarios: [],
  };
  if (typeof tpl.build === 'function') {
    return tpl.build(base, state.wizard.data);
  }
  return base;
}
if (document.readyState === 'complete' || document.readyState === 'interactive') {
    init();
} else {
    window.addEventListener('load', init);
}
})();
