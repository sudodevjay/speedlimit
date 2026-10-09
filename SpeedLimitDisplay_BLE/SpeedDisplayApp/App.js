import React, { useState } from 'react';
import { SafeAreaView, StatusBar } from 'react-native';
import HomeScreen from './src/screens/HomeScreen';
import ScanScreen from './src/screens/ScanScreen';
import ControlScreen from './src/screens/ControlScreen';
import { colors } from './src/ui';

// Tiny state-based navigation: home -> scan -> control.
export default function App() {
  const [route, setRoute] = useState({ name: 'home' });

  let screen;
  if (route.name === 'scan') {
    screen = (
      <ScanScreen
        onBack={() => setRoute({ name: 'home' })}
        onAdded={(display, session) => setRoute({ name: 'control', display, session })}
      />
    );
  } else if (route.name === 'control') {
    screen = (
      <ControlScreen
        display={route.display}
        initialSession={route.session}
        onBack={() => setRoute({ name: 'home' })}
      />
    );
  } else {
    screen = (
      <HomeScreen
        onAdd={() => setRoute({ name: 'scan' })}
        onOpen={(display) => setRoute({ name: 'control', display })}
      />
    );
  }

  return (
    <SafeAreaView style={{ flex: 1, backgroundColor: colors.bg }}>
      <StatusBar barStyle="light-content" backgroundColor={colors.bg} />
      {screen}
    </SafeAreaView>
  );
}
