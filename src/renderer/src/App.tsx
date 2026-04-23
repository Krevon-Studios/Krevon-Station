import { Island } from './components/Island'
import { Taskbar } from './components/Taskbar'

export default function App() {
  return (
    <div className="w-full h-full overflow-hidden flex flex-col relative">
      <Taskbar />
      <div className="absolute top-0 w-full flex justify-center pointer-events-none z-20">
        <div className="pointer-events-none w-full">
          <Island />
        </div>
      </div>
    </div>
  )
}
