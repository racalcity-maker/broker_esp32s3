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
