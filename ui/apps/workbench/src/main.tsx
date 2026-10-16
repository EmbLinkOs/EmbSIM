// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import '@embsim/design/base.css';
import '@embsim/scene/scene.css';
import '@embsim/instruments/instruments.css';
import './styles/app.css';
import { App } from './App';

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <App />
  </StrictMode>,
);
