import React, { useCallback, useEffect, useState } from 'react';
import { FlatList, Pressable, Text, View } from 'react-native';
import { listDisplays } from '../storage';
import { Button, colors, styles } from '../ui';

export default function HomeScreen({ onAdd, onOpen }) {
  const [displays, setDisplays] = useState([]);

  const load = useCallback(() => {
    listDisplays()
      .then((list) => setDisplays(list.sort((a, b) => a.name.localeCompare(b.name))))
      .catch(() => setDisplays([]));
  }, []);

  useEffect(load, [load]);

  return (
    <View style={styles.screen}>
      <View style={styles.header}>
        <Text style={styles.title}>Speed Displays</Text>
      </View>

      <FlatList
        data={displays}
        keyExtractor={(d) => d.mac}
        ListEmptyComponent={
          <Text style={[styles.small, { textAlign: 'center', marginTop: 40 }]}>
            No displays yet. Tap "Add display" and enter the device key shown on the panel.
          </Text>
        }
        renderItem={({ item }) => (
          <Pressable onPress={() => onOpen(item)} style={[styles.card, styles.row]}>
            <View style={{ flex: 1 }}>
              <Text style={[styles.text, { fontWeight: '600' }]}>{item.name}</Text>
              <Text style={styles.small}>
                {item.transport === 'wifi' ? `Wi-Fi · ${item.host}` : `Bluetooth · ${item.mac}`}
              </Text>
            </View>
            <View style={signStyle}>
              <Text style={{ color: '#000', fontSize: 20, fontWeight: '800' }}>
                {item.lastLimit ?? '--'}
              </Text>
            </View>
          </Pressable>
        )}
      />

      <Button title="+ Add display" onPress={onAdd} />
    </View>
  );
}

const signStyle = {
  width: 52,
  height: 52,
  borderRadius: 26,
  borderWidth: 5,
  borderColor: colors.accent,
  backgroundColor: '#fff',
  alignItems: 'center',
  justifyContent: 'center',
};
