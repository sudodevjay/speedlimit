import React, { useEffect, useRef, useState } from 'react';
import { FlatList, Modal, Pressable, Text, TextInput, View } from 'react-native';
import { DisplaySession, ensureBlePermissions, ensureBluetoothOn, startScan } from '../ble';
import { isValidKey, normalizeKey } from '../protocol';
import { listDisplays, saveDisplay } from '../storage';
import { Button, colors, styles } from '../ui';

const SCAN_MS = 15000;

export default function ScanScreen({ onBack, onAdded }) {
  const [found, setFound] = useState({});
  const [saved, setSaved] = useState(new Set());
  const [scanning, setScanning] = useState(false);
  const [error, setError] = useState('');
  const [selected, setSelected] = useState(null);
  const [key, setKey] = useState('');
  const [busy, setBusy] = useState(false);
  const stopRef = useRef(null);

  const stop = () => {
    stopRef.current && stopRef.current();
    stopRef.current = null;
    setScanning(false);
  };

  const scan = async () => {
    setError('');
    setFound({});
    try {
      if (!(await ensureBlePermissions())) throw new Error('Bluetooth permission denied');
      await ensureBluetoothOn();
    } catch (e) {
      setError(e.message);
      return;
    }
    setScanning(true);
    stopRef.current = startScan(
      (d) => setFound((prev) => ({ ...prev, [d.id]: { id: d.id, name: d.localName || d.name || 'Unknown', rssi: d.rssi } })),
      (e) => {
        setError(e.message);
        stop();
      }
    );
    setTimeout(stop, SCAN_MS);
  };

  useEffect(() => {
    listDisplays().then((l) => setSaved(new Set(l.map((d) => d.mac))));
    scan();
    return stop;
  }, []);

  const connect = async () => {
    if (!isValidKey(key)) {
      setError('Device key must be 8-32 letters/numbers');
      return;
    }
    stop();
    setBusy(true);
    setError('');
    try {
      const session = await DisplaySession.open(selected.id, key);
      const record = {
        mac: session.info.mac,
        id: selected.id,
        name: session.state?.name || selected.name,
        key: normalizeKey(key),
        lastLimit: session.state?.limit,
      };
      await saveDisplay(record);
      setSelected(null);
      onAdded(record, session);
    } catch (e) {
      setError(e.message || String(e));
    } finally {
      setBusy(false);
    }
  };

  const devices = Object.values(found).sort((a, b) => a.name.localeCompare(b.name));

  return (
    <View style={styles.screen}>
      <View style={styles.header}>
        <Pressable onPress={onBack}>
          <Text style={styles.back}>‹ Back</Text>
        </Pressable>
        <Text style={styles.title}>Add display</Text>
      </View>

      {!!error && !selected && <Text style={styles.error}>{error}</Text>}

      <FlatList
        data={devices}
        keyExtractor={(d) => d.id}
        ListEmptyComponent={
          <Text style={[styles.small, { textAlign: 'center', marginTop: 40 }]}>
            {scanning ? 'Searching for displays…' : 'No displays found.'}
          </Text>
        }
        renderItem={({ item }) => (
          <Pressable
            onPress={() => {
              setError('');
              setKey('');
              setSelected(item);
            }}
            style={[styles.card, styles.row]}
          >
            <View style={{ flex: 1 }}>
              <Text style={[styles.text, { fontWeight: '600' }]}>{item.name}</Text>
              <Text style={styles.small}>
                {item.id} · {item.rssi} dBm
              </Text>
            </View>
            {saved.has(item.id.toUpperCase()) && <Text style={{ color: colors.ok }}>Added</Text>}
          </Pressable>
        )}
      />

      <Button
        title={scanning ? 'Scanning…' : 'Scan again'}
        kind="secondary"
        disabled={scanning}
        onPress={scan}
      />

      <Modal visible={!!selected} transparent animationType="fade" onRequestClose={() => setSelected(null)}>
        <View style={{ flex: 1, backgroundColor: '#000a', justifyContent: 'center', padding: 24 }}>
          <View style={styles.card}>
            <Text style={[styles.text, { fontWeight: '700', marginBottom: 4 }]}>{selected?.name}</Text>
            <Text style={[styles.small, { marginBottom: 12 }]}>
              Enter the device key (shown on the panel at first power-on, or in the Serial Monitor).
            </Text>
            <TextInput
              style={styles.input}
              value={key}
              onChangeText={(t) => setKey(t.toUpperCase())}
              placeholder="e.g. K7P3MX9Q"
              placeholderTextColor={colors.muted}
              autoCapitalize="characters"
              autoCorrect={false}
              secureTextEntry
              maxLength={32}
            />
            {!!error && <Text style={styles.error}>{error}</Text>}
            <View style={[styles.row, { marginTop: 12 }]}>
              <Button title="Cancel" kind="secondary" style={{ flex: 1 }} onPress={() => setSelected(null)} />
              <Button title="Connect" style={{ flex: 1 }} busy={busy} onPress={connect} />
            </View>
          </View>
        </View>
      </Modal>
    </View>
  );
}
