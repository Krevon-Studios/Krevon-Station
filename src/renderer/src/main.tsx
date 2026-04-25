import React from 'react'
import ReactDOM from 'react-dom/client'
import App from './App'
import './index.css'
import { applyAccentVars } from './utils/accent'

window.island.getAccentColor().then(applyAccentVars)
window.island.onAccentColor(applyAccentVars)

ReactDOM.createRoot(document.getElementById('root')!).render(
  <React.StrictMode>
    <App />
  </React.StrictMode>
)
