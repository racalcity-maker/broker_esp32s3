// NOTE: This bundle is generated from assets/wizard/*.js via build_devices_wizard.py.
//       Edit the source modules rather than the assembled devices_wizard.js.
(() => {
const ACTION_TYPES = ['mqtt_publish','audio_play','audio_stop','set_flag','wait_flags','loop','delay','event','nop'];
const TEMPLATE_TYPES = [
  {value: '', label: 'No template'},
  {value: 'uid_validator', label: 'UID validator'},
  {value: 'signal_hold', label: 'Signal hold'},
  {value: 'on_mqtt_event', label: 'MQTT trigger'},
  {value: 'on_flag', label: 'Flag trigger'},
  {value: 'if_condition', label: 'Conditional scenario'},
  {value: 'interval_task', label: 'Interval task'},
  {value: 'sequence_lock', label: 'Sequence lock'},
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
      base.topics = [];
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
  model: null,
  profiles: [],
  activeProfile: '',
  actionsRoot: null,
  selectedDevice: -1,
  selectedScenario: -1,
  busy: false,
  dirty: false,
  jsonVisible: false,
  wizard: {
    open: false,
    step: 0,
    template: null,
    data: {},
    autoId: true,
  },
  wizardModal: null,
  wizardContent: null,
};

let initialized = false;

function init() {
  if (initialized) {
    return;
  }
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
      <div class="dw-layout">
        <div class="dw-list" id="dw_device_list"></div>
        <div class="dw-detail" id="dw_device_detail"></div>
      </div>
      <div class="dw-json collapsed" id="dw_json_panel">
        <pre class="dw-json-content" id="dw_json_preview">No config</pre>
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
    if (!btn) return;
    runScenario(btn.dataset.runDevice, btn.dataset.runScenario);
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
    case 'add-topic': addTopic(); break;
    case 'remove-topic': removeTopic(btn.dataset.index); break;
    case 'add-scenario': addScenario(); break;
    case 'remove-scenario': removeScenario(btn.dataset.index); break;
    case 'select-scenario': selectScenario(btn.dataset.index); break;
    case 'add-step': addStep(); break;
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
  }
}

function handleDetailInput(ev) {
  const el = ev.target;
  if (!el) return;
  if (el.dataset.deviceField) {
    updateDeviceField(el.dataset.deviceField, el.value);
    return;
  }
  if (el.dataset.topicField) {
    updateTopicField(el.dataset.index, el.dataset.topicField, el.value);
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
