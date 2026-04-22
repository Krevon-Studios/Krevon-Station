/** @type {import('tailwindcss').Config} */
module.exports = {
  content: ['./src/renderer/src/**/*.{ts,tsx}'],
  theme: {
    extend: {
      colors: {
        island: {
          bg: 'rgba(8,8,8,0.92)',
          border: 'rgba(255,255,255,0.07)',
          accent: '#7C6AFF',
          success: '#34D399',
          muted: 'rgba(255,255,255,0.45)'
        }
      },
      fontFamily: {
        sans: ['Inter', 'Segoe UI Variable Text', 'Segoe UI', 'system-ui', 'sans-serif']
      }
    }
  },
  plugins: []
}
