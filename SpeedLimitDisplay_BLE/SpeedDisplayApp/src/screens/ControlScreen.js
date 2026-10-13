import React, { useEffect, useRef, useState } from 'react';
import { Alert, Pressable, ScrollView, Text, TextInput, View } from 'react-native';
import { isWifi, openSession } from '../session';
import { COLOR_PRESETS, MAX_LIMIT, QUICK_LIMITS } from '../config';
import { isValidKey, normalizeKey } from '../protocol';
import { removeDisplay, updateDisplay } from '../storage';
import { Button, colors, styles } from '../ui';

export default function ControlScreen({ display, initialSession, onBack }) {
  const sessionRef = useRef(initialSession || null);
  const [record, setRecord] = useState(display);
  const [state, setState] = useState(initialSession?.state || null);
  const [status, setStatus] = useState(initialSession ? 'connected' : 'connecting');
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(false);
  const [limitText, setLimitText] = useState(String(initialSession?.state?.limit ?? display.lastLimit ?? ''));
  const [newName, setNewName] = useState('');
  const [newKey, setNewKey] = useState('');

  const attach = (s) => {
    sessionRef.current = s;
    s.onDisconnected = () => setStatus('disconnected');
    setStatus('connected');
    setState(s.state);
    if (s.state) setLimitText(String(s.state.limit));
  };

  const ensureSession = async () => {
    const cur = sessionRef.current;
    if (cur && cur.connected) return cur;
    setStatus('connecting');
    const s = await openSession(record);
    attach(s);
    return s;
  };

  // Runs one action on the display, reconnecting first if needed.
  const run = async (fn, after) => {
    setBusy(true);
    setError('');
    try {
      const s = await ensureSession();
      const st = await fn(s);
      if (st) {
        setState(st);
        setLimitText(String(st.limit));
        await updateDisplay(record.mac, { lastLimit: st.limit, name: st.name });
      }
      after && (await after());
    } catch (e) {
      setError(e.message || String(e));
      if (!sessionRef.current?.connected) setStatus('disconnected');
    } finally {
      setBusy(false);
    }
  };

  useEffect(() => {
    if (initialSession) attach(initialSession);
    else run(async (s) => s.state);
    return () => {
      sessionRef.current && sessionRef.current.close();
    };
  }, []);

  const sendLimit = (v) => {
    const n = Number(v);
    if (!Number.isInteger(n) || n < 0 || n > MAX_LIMIT) {
      setError(`Speed limit must be 0-${MAX_LIMIT}`);
      return;
    }
    run((s) => s.setLimit(n));
  };

  const step = (d) => {
    const n = Math.min(MAX_LIMIT, Math.max(0, (Number(limitText) || 0) + d));
    setLimitText(String(n));
  };

  const rename = () => {
    if (!/^[A-Za-z0-9_-]{1,20}$/.test(newName)) {
      setError('Name: 1-20 chars (A-Z a-z 0-9 _ -)');
      return;
    }
    run(
      (s) => s.rename(newName),
      async () => {
        await updateDisplay(record.mac, { name: newName });
        setRecord((r) => ({ ...r, name: newName }));
        setNewName('');
        Alert.alert('Renamed', isWifi(record) ? 'Name saved.' : 'The display is restarting with the new name.');
      }
    );
  };

  const changeKey = () => {
    if (!isValidKey(newKey)) {
      setError('Key: 8-32 letters/numbers');
      return;
    }
    const k = normalizeKey(newKey);
    Alert.alert('Change device key?', `New key: ${k}\nWrite it down. Other phones will need this key.`, [
      { text: 'Cancel', style: 'cancel' },
      {
        text: 'Change',
        style: 'destructive',
        onPress: () =>
          run(
            async (s) => {
              await s.changeKey(k);
              return null;
            },
            async () => {
              await updateDisplay(record.mac, { key: k });
              setRecord((r) => ({ ...r, key: k }));
              setNewKey('');
              await sessionRef.current?.close();
              setStatus('disconnected');
              Alert.alert('Key changed', 'The display now only accepts the new key.');
            }
          ),
      },
    ]);
  };

  const remove = () => {
    Alert.alert('Remove display?', `${record.name} will be removed from this phone.`, [
      { text: 'Cancel', style: 'cancel' },
      {
        text: 'Remove',
        style: 'destructive',
        onPress: async () => {
          await removeDisplay(record.mac);
          onBack();
        },
      },
    ]);
  };

  const statusColor = status === 'connected' ? colors.ok : status === 'connecting' ? colors.warn : colors.muted;

  return (
    <ScrollView style={styles.screen} contentContainerStyle={{ paddingBottom: 40 }} keyboardShouldPersistTaps="handled">
      <View style={styles.header}>
        <Pressable onPress={onBack}>
          <Text style={styles.back}>‹ Back</Text>
        </Pressable>
        <Text style={styles.title}>{record.name}</Text>
      </View>

      <View style={[styles.row, { marginBottom: 12 }]}>
        <View style={{ width: 10, height: 10, borderRadius: 5, backgroundColor: statusColor }} />
        <Text style={styles.small}>
          {status} · {isWifi(record) ? `Wi-Fi ${record.host}` : record.mac}
        </Text>
        <View style={{ flex: 1 }} />
        {status !== 'connected' && (
          <Button title="Connect" kind="secondary" busy={busy} onPress={() => run(async (s) => s.state)} />
        )}
      </View>

      {!!error && <Text style={styles.error}>{error}</Text>}

      {/* Display mode (Raspberry Pi only): speed limit or videos from the USB stick */}
      {state?.mode !== undefined && (
        <View style={styles.card}>
          <Text style={styles.label}>Display shows</Text>
          <View style={styles.row}>
            {[
              ['speed', 'Speed limit'],
              ['video', 'Video (USB)'],
            ].map(([m, title]) => (
              <Button
                key={m}
                title={title}
                kind={state.mode === m ? 'primary' : 'secondary'}
                style={{ flex: 1 }}
                disabled={busy}
                onPress={() => run((s) => s.setMode(m))}
              />
            ))}
          </View>
          <Text style={[styles.small, { marginTop: 8 }]}>
            {state.videos ? `${state.videos} video(s) on the USB stick` : 'No videos found on the USB stick'}
            {state.mode === 'video' && (state.playing ? ' · playing now' : ' · showing the speed limit instead')}
          </Text>
          <Button title="Refresh" kind="secondary" style={{ marginTop: 8 }} busy={busy} onPress={() => run((s) => s.send('GET'))} />
        </View>
      )}

      {/* Speed limit */}
      <View style={styles.card}>
        <Text style={styles.label}>Speed limit (currently on display: {state?.limit ?? '--'})</Text>
        <View style={styles.row}>
          <Button title="−5" kind="secondary" onPress={() => step(-5)} />
          <TextInput
            style={[styles.input, { flex: 1, textAlign: 'center', fontSize: 28, fontWeight: '800' }]}
            value={limitText}
            onChangeText={(t) => setLimitText(t.replace(/[^0-9]/g, '').slice(0, 2))}
            keyboardType="number-pad"
            maxLength={2}
          />
          <Button title="+5" kind="secondary" onPress={() => step(5)} />
        </View>
        <View style={[styles.row, { flexWrap: 'wrap', marginTop: 10 }]}>
          {QUICK_LIMITS.map((v) => (
            <Pressable
              key={v}
              onPress={() => setLimitText(String(v))}
              style={{
                paddingVertical: 8,
                paddingHorizontal: 12,
                borderRadius: 8,
                backgroundColor: String(v) === limitText ? colors.accent : colors.bg,
                borderWidth: 1,
                borderColor: colors.border,
              }}
            >
              <Text style={styles.text}>{v}</Text>
            </Pressable>
          ))}
        </View>
        <Button title="Set speed limit" style={{ marginTop: 12 }} busy={busy} onPress={() => sendLimit(limitText)} />
      </View>

      {/* Brightness */}
      <View style={styles.card}>
        <Text style={styles.label}>Brightness: {state?.brightness ?? '--'} / 255</Text>
        <View style={styles.row}>
          {[25, 60, 125, 190, 255].map((v) => (
            <Button
              key={v}
              title={`${Math.round((v / 255) * 100)}%`}
              kind={state?.brightness === v ? 'primary' : 'secondary'}
              style={{ flex: 1, paddingHorizontal: 4 }}
              disabled={busy}
              onPress={() => run((s) => s.setBrightness(v))}
            />
          ))}
        </View>
      </View>

      {/* Colours */}
      <View style={styles.card}>
        {[
          ['Digit colour', 'digitColor'],
          ['"SPEED LIMIT" text colour', 'textColor'],
        ].map(([label, field]) => (
          <View key={field} style={{ marginBottom: 8 }}>
            <Text style={styles.label}>{label}</Text>
            <View style={styles.row}>
              {COLOR_PRESETS.map((c, i) => (
                <Pressable
                  key={c.name}
                  disabled={busy || !state}
                  onPress={() =>
                    run((s) =>
                      field === 'digitColor' ? s.setColors(i, state.textColor) : s.setColors(state.digitColor, i)
                    )
                  }
                  style={{
                    width: 36,
                    height: 36,
                    borderRadius: 18,
                    backgroundColor: c.hex,
                    borderWidth: state?.[field] === i ? 3 : 0,
                    borderColor: colors.text,
                  }}
                />
              ))}
            </View>
          </View>
        ))}
      </View>

      {/* Settings */}
      <View style={styles.card}>
        <Text style={styles.label}>
          {isWifi(record) ? 'Rename display' : 'Rename display (BLE name, display restarts)'}
        </Text>
        <View style={styles.row}>
          <TextInput
            style={[styles.input, { flex: 1 }]}
            value={newName}
            onChangeText={setNewName}
            placeholder={record.name}
            placeholderTextColor={colors.muted}
            autoCapitalize="none"
            autoCorrect={false}
            maxLength={20}
          />
          <Button title="Save" kind="secondary" busy={busy} onPress={rename} />
        </View>

        <Text style={[styles.label, { marginTop: 14 }]}>Change device key</Text>
        <View style={styles.row}>
          <TextInput
            style={[styles.input, { flex: 1 }]}
            value={newKey}
            onChangeText={(t) => setNewKey(t.toUpperCase())}
            placeholder="new key, 8-32 chars"
            placeholderTextColor={colors.muted}
            autoCapitalize="characters"
            autoCorrect={false}
            maxLength={32}
          />
          <Button title="Change" kind="secondary" busy={busy} onPress={changeKey} />
        </View>
      </View>

      <Button title="Remove from this phone" kind="danger" onPress={remove} />
    </ScrollView>
  );
}
