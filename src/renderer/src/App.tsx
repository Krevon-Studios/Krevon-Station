import { Island }  from './components/Island'
import { Taskbar } from './components/Taskbar'
import { Drawer }  from './components/Drawer'

export default function App() {
  const view = new URLSearchParams(window.location.search).get('view') ?? 'island'

  if (view === 'taskbar') {
    return (
      <div className="w-full h-full overflow-hidden flex flex-col relative">
        <Taskbar />
      </div>
    )
  }

  if (view === 'drawer') {
    return (
      <div className="w-full h-full overflow-hidden">
        <Drawer />
      </div>
    )
  }

  return (
    <div className="w-full h-full overflow-hidden flex flex-col relative">
      <div className="absolute top-0 w-full flex justify-center pointer-events-none z-20">
        <div className="pointer-events-none w-full">
          <Island />
        </div>
      </div>
    </div>
  )
}
