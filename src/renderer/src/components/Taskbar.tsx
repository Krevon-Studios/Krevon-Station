import { useState, useEffect } from 'react'
import { Wifi, Volume2, Power } from 'lucide-react'

export function Taskbar() {
  const [count, setCount] = useState(1)
  const [activeIndex, setActiveIndex] = useState(0)

  useEffect(() => {
    // Pull current state immediately (avoids did-finish-load race condition)
    window.island.getVirtualDesktops().then((data) => {
      setCount(data.count)
      setActiveIndex(data.activeIndex)
    })

    // Then listen for subsequent changes
    return window.island.onVirtualDesktops((data) => {
      setCount(data.count)
      setActiveIndex(data.activeIndex)
    })
  }, [])

  return (
    <div
      className="w-full h-[32px] bg-black flex items-center justify-between px-4 select-none relative z-10"
      style={{
        WebkitAppRegion: 'no-drag',
      } as React.CSSProperties}
    >

      {/* Left side: Pagination dots */}
      <div className="flex items-center gap-[2px] h-full cursor-pointer opacity-80 hover:opacity-100 transition-opacity pl-2">
        {Array.from({ length: count }).map((_, i) => (
          <div
            key={i}
            className="py-[6px] px-[1.5px] group cursor-pointer"
            onClick={() => window.island.switchVirtualDesktop(i)}
          >
            <div className={`h-[6px] rounded-full transition-all duration-300 bg-white ${
              i === activeIndex ? 'w-[18px]' : 'w-[6px] opacity-40 group-hover:opacity-100'
            }`} />
          </div>
        ))}
      </div>

      {/* Center is covered by the Dynamic Island */}

      {/* Right side: System tray icons and time */}
      <button
        className="flex items-center gap-[12px] opacity-85 hover:opacity-100 transition-all duration-200 hover:bg-white/5 px-3 py-[5px] rounded-xl cursor-pointer"
        onClick={() => console.log('Open menu panel')}
      >
        <Wifi size={14} color="#d4d4d4" />
        <Volume2 size={14} color="#d4d4d4" />
        <Power size={14} color="#d4d4d4" />
      </button>

    </div>
  )
}
