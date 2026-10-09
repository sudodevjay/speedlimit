import React from 'react';
import { ActivityIndicator, Pressable, StyleSheet, Text } from 'react-native';

export const colors = {
  bg: '#0f1115',
  card: '#1a1d24',
  border: '#2a2f3a',
  text: '#f2f4f8',
  muted: '#8a93a6',
  accent: '#e53935',
  ok: '#2ecc71',
  warn: '#f5a623',
};

export function Button({ title, onPress, disabled, busy, kind = 'primary', style }) {
  const bg = kind === 'primary' ? colors.accent : kind === 'danger' ? '#5c1d1d' : colors.card;
  return (
    <Pressable
      onPress={onPress}
      disabled={disabled || busy}
      style={({ pressed }) => [
        styles.btn,
        { backgroundColor: bg, opacity: disabled ? 0.4 : pressed ? 0.7 : 1 },
        style,
      ]}
    >
      {busy ? <ActivityIndicator color="#fff" /> : <Text style={styles.btnText}>{title}</Text>}
    </Pressable>
  );
}

export const styles = StyleSheet.create({
  screen: { flex: 1, backgroundColor: colors.bg, padding: 16 },
  header: { flexDirection: 'row', alignItems: 'center', marginBottom: 16 },
  title: { color: colors.text, fontSize: 22, fontWeight: '700', flex: 1 },
  back: { color: colors.muted, fontSize: 16, marginRight: 12 },
  card: {
    backgroundColor: colors.card,
    borderColor: colors.border,
    borderWidth: 1,
    borderRadius: 12,
    padding: 14,
    marginBottom: 12,
  },
  label: { color: colors.muted, fontSize: 13, marginBottom: 6 },
  text: { color: colors.text, fontSize: 16 },
  small: { color: colors.muted, fontSize: 12 },
  input: {
    backgroundColor: colors.bg,
    color: colors.text,
    borderColor: colors.border,
    borderWidth: 1,
    borderRadius: 8,
    paddingHorizontal: 12,
    paddingVertical: 10,
    fontSize: 16,
  },
  row: { flexDirection: 'row', alignItems: 'center', gap: 8 },
  btn: {
    paddingVertical: 12,
    paddingHorizontal: 16,
    borderRadius: 10,
    alignItems: 'center',
    justifyContent: 'center',
  },
  btnText: { color: '#fff', fontSize: 16, fontWeight: '600' },
  error: { color: '#ff6b6b', marginVertical: 8 },
});
