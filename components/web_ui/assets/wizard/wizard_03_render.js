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
