import { useState, useEffect, useCallback, useRef } from "react";

// ── Default bookmarks — matches bookmarks.cpp LoadBookmarks() defaults ──
const DEFAULT_BOOKMARKS = [
  { title: "YouTube",   url: "https://www.youtube.com" },
  { title: "Facebook",  url: "https://www.facebook.com" },
  { title: "GitHub",    url: "https://github.com" },
  { title: "Wikipedia", url: "https://www.wikipedia.org" },
  { title: "ChatGPT",   url: "https://chatgpt.com" },
];

// ── ShortTitle — matches C++ ShortTitle() ──
function shortTitle(title, url) {
  if (title && title.length <= 20) return title;
  try {
    let u = new URL(url).hostname;
    if (u.startsWith("www.")) u = u.slice(4);
    if (u.length > 16) u = u.slice(0, 14) + "..";
    return u;
  } catch {
    return title?.slice(0, 16) || url.slice(0, 16);
  }
}

// ── Favicon letter — first char of title (like C++ DrawBookmarkPanel) ──
function faviconLetter(title) {
  return (title || "?")[0].toUpperCase();
}

// ── Accent color matches C++ cAccent = teal #0CA8B0 ──
const TEAL = "#0CA8B0";

export default function RasBookmarkBar() {
  const [bookmarks, setBookmarks]       = useState(DEFAULT_BOOKMARKS);
  const [panelOpen, setPanelOpen]       = useState(false);
  const [isDark, setIsDark]             = useState(true);
  const [currentUrl, setCurrentUrl]     = useState("https://www.google.com");
  const [currentTitle, setCurrentTitle] = useState("Google");
  const [toast, setToast]               = useState(null);   // { msg, type }
  const [addDialogOpen, setAddDialogOpen] = useState(false);
  const [dialogTitle, setDialogTitle]   = useState("");
  const [dialogUrl, setDialogUrl]       = useState("");
  const [urlInput, setUrlInput]         = useState("");
  const panelRef  = useRef(null);
  const toastTimer = useRef(null);

  // ── isBookmarked — matches C++ IsBookmarked() ──
  const isBookmarked = useCallback(
    (url) => bookmarks.some((b) => b.url === url),
    [bookmarks]
  );

  // ── AddBookmark — matches C++ AddBookmark() ──
  const addBookmark = useCallback((title, url) => {
    if (!url || url === "about:blank") return;
    setBookmarks((prev) => {
      if (prev.some((b) => b.url === url)) return prev;
      return [{ title: title || url, url }, ...prev];
    });
  }, []);

  // ── RemoveBookmark — matches C++ RemoveBookmark() ──
  const removeBookmark = useCallback((index) => {
    setBookmarks((prev) => prev.filter((_, i) => i !== index));
  }, []);

  // ── ToggleBookmark — matches C++ ToggleBookmark() ──
  const toggleBookmark = useCallback((url, title) => {
    setBookmarks((prev) => {
      const idx = prev.findIndex((b) => b.url === url);
      if (idx !== -1) {
        showToast("Bookmark removed", "remove");
        return prev.filter((_, i) => i !== idx);
      }
      showToast("Bookmark added", "add");
      return [{ title: title || url, url }, ...prev];
    });
  }, []);

  function showToast(msg, type = "add") {
    setToast({ msg, type });
    clearTimeout(toastTimer.current);
    toastTimer.current = setTimeout(() => setToast(null), 2200);
  }

  // ── Ctrl+D — matches keyboard_shortcuts.cpp Ctrl+D handler ──
  useEffect(() => {
    function onKeyDown(e) {
      if ((e.ctrlKey || e.metaKey) && e.key === "d") {
        e.preventDefault();
        // Open add-bookmark dialog (Chrome-style popup)
        setDialogTitle(currentTitle);
        setDialogUrl(currentUrl);
        setAddDialogOpen(true);
      }
      if (e.key === "Escape") {
        setPanelOpen(false);
        setAddDialogOpen(false);
      }
    }
    window.addEventListener("keydown", onKeyDown);
    return () => window.removeEventListener("keydown", onKeyDown);
  }, [currentUrl, currentTitle, isBookmarked, toggleBookmark]);

  // ── Close panel on outside click ──
  useEffect(() => {
    function onClick(e) {
      if (panelRef.current && !panelRef.current.contains(e.target)) {
        setPanelOpen(false);
      }
    }
    if (panelOpen) document.addEventListener("mousedown", onClick);
    return () => document.removeEventListener("mousedown", onClick);
  }, [panelOpen]);

  const dark  = isDark;
  const bg    = dark ? "#20212600" : "#f1f3f4";
  const bar   = dark ? "#292a30" : "#ffffff";
  const text  = dark ? "#e2e2e8" : "#202124";
  const dim   = dark ? "#888898" : "#787880";
  const hover = dark ? "#3a3b44" : "#e8eaed";
  const panel = dark ? "#22232a" : "#ffffff";
  const panelBorder = dark ? "#3c3d48" : "#dadce0";
  const panelHeader = dark ? "#2a2b34" : "#f6f8fa";
  const inputBg = dark ? "#2e2f3a" : "#f1f3f4";

  // ── Simulate navigating to a bookmark ──
  function navigateTo(url, title) {
    setCurrentUrl(url);
    setCurrentTitle(title || shortTitle("", url));
    setUrlInput(url);
    setPanelOpen(false);
    showToast(`Navigated to ${shortTitle(title, url)}`, "nav");
  }

  // ── Confirm Ctrl+D dialog ──
  function confirmAdd() {
    if (!dialogUrl) return;
    if (isBookmarked(dialogUrl)) {
      // already bookmarked — remove
      const idx = bookmarks.findIndex((b) => b.url === dialogUrl);
      if (idx !== -1) removeBookmark(idx);
      showToast("Bookmark removed", "remove");
    } else {
      addBookmark(dialogTitle, dialogUrl);
      showToast("Bookmark added", "add");
    }
    setAddDialogOpen(false);
  }

  // ── Quick navigate from URL bar ──
  function handleNavigate(e) {
    e.preventDefault();
    let url = urlInput.trim();
    if (!url) return;
    if (!url.startsWith("http")) url = "https://" + url;
    const title = new URL(url).hostname.replace(/^www\./, "");
    setCurrentUrl(url);
    setCurrentTitle(title);
    showToast(`Navigated to ${title}`, "nav");
  }

  const starred = isBookmarked(currentUrl);

  return (
    <div
      style={{
        fontFamily: "'Segoe UI', system-ui, sans-serif",
        background: dark ? "#1e1f26" : "#f8f9fa",
        minHeight: "100vh",
        padding: 0,
        color: text,
        position: "relative",
      }}
      tabIndex={0}
    >
      {/* ── TOOLBAR ── */}
      <div
        style={{
          background: bar,
          borderBottom: `1px solid ${panelBorder}`,
          padding: "8px 12px",
          display: "flex",
          alignItems: "center",
          gap: 8,
        }}
      >
        {/* Nav buttons */}
        {["←", "→", "↻"].map((ic, i) => (
          <button key={i}
            title={["Back","Forward","Reload"][i]}
            style={{
              background: "none", border: "none", color: dim,
              cursor: "pointer", fontSize: 16, padding: "4px 6px",
              borderRadius: 4, lineHeight: 1,
              transition: "background .15s",
            }}
            onMouseEnter={e => e.target.style.background = hover}
            onMouseLeave={e => e.target.style.background = "none"}
          >{ic}</button>
        ))}

        {/* Address bar */}
        <form onSubmit={handleNavigate} style={{ flex: 1, display: "flex" }}>
          <div style={{
            flex: 1, display: "flex", alignItems: "center",
            background: dark ? "#3a3b46" : "#f1f3f4",
            borderRadius: 22, padding: "0 12px",
            border: `1px solid ${panelBorder}`,
          }}>
            <span style={{ fontSize: 12, color: dim, marginRight: 6 }}>🔒</span>
            <input
              value={urlInput || currentUrl}
              onChange={e => setUrlInput(e.target.value)}
              onFocus={e => { e.target.select(); setUrlInput(currentUrl); }}
              style={{
                flex: 1, background: "none", border: "none", outline: "none",
                color: text, fontSize: 13, padding: "6px 0",
              }}
            />
            {/* Bookmark star — matches C++ DrawBookmarkStar() */}
            <button
              type="button"
              title={starred ? "Remove bookmark (Ctrl+D)" : "Add bookmark (Ctrl+D)"}
              onClick={() => {
                setDialogTitle(currentTitle);
                setDialogUrl(currentUrl);
                setAddDialogOpen(true);
              }}
              style={{
                background: "none", border: "none", cursor: "pointer",
                fontSize: 16, padding: "2px 4px", lineHeight: 1,
                color: starred ? "#f4c430" : dim,
                transition: "color .15s",
              }}
            >
              {starred ? "★" : "☆"}
            </button>
          </div>
        </form>

        {/* Dark mode toggle */}
        <button
          onClick={() => setIsDark(d => !d)}
          style={{
            background: dark ? "#3a3b46" : "#e8eaed",
            border: "none", borderRadius: 20, padding: "5px 12px",
            cursor: "pointer", fontSize: 12, color: text,
          }}
        >
          {dark ? "☀ Light" : "🌙 Dark"}
        </button>

        {/* Bookmarks panel toggle — matches C++ Ctrl+B */}
        <button
          onClick={() => setPanelOpen(o => !o)}
          title="Bookmarks (Ctrl+B)"
          style={{
            background: panelOpen ? TEAL : "none",
            border: "none", borderRadius: 4, padding: "5px 8px",
            cursor: "pointer", fontSize: 16, color: panelOpen ? "#fff" : dim,
            transition: "background .15s, color .15s",
          }}
        >
          🔖
        </button>
      </div>

      {/* ── BOOKMARK BAR — matches C++ DrawBookmarkBar() ── */}
      <div style={{
        background: bar,
        borderBottom: `1px solid ${panelBorder}`,
        padding: "2px 8px",
        display: "flex",
        alignItems: "center",
        gap: 2,
        minHeight: 32,
        overflowX: "auto",
        scrollbarWidth: "none",
      }}>
        {bookmarks.length === 0 && (
          <span style={{ fontSize: 12, color: dim, padding: "4px 8px" }}>
            No bookmarks — press Ctrl+D to add one
          </span>
        )}
        {bookmarks.map((b, i) => {
          const label = shortTitle(b.title, b.url);
          return (
            <button
              key={i}
              onClick={() => navigateTo(b.url, b.title)}
              onContextMenu={e => { e.preventDefault(); removeBookmark(i); showToast("Bookmark removed","remove"); }}
              title={`${b.title}\n${b.url}\n\nRight-click to remove`}
              style={{
                display: "flex", alignItems: "center", gap: 4,
                background: "none", border: "none", borderRadius: 4,
                padding: "3px 8px", cursor: "pointer", whiteSpace: "nowrap",
                color: text, fontSize: 12,
                transition: "background .15s",
                flexShrink: 0,
              }}
              onMouseEnter={e => e.currentTarget.style.background = hover}
              onMouseLeave={e => e.currentTarget.style.background = "none"}
            >
              {/* Favicon (globe icon) */}
              <span style={{
                width: 14, height: 14, borderRadius: "50%",
                background: dark ? "#3a3b4a" : "#e8eaed",
                display: "flex", alignItems: "center", justifyContent: "center",
                fontSize: 9, color: TEAL, fontWeight: 700, flexShrink: 0,
              }}>
                {faviconLetter(b.title)}
              </span>
              {label}
            </button>
          );
        })}
      </div>

      {/* ── PAGE CONTENT (simulated) ── */}
      <div style={{ padding: 32, textAlign: "center" }}>
        <div style={{
          display: "inline-block", borderRadius: 12,
          background: dark ? "#2a2b34" : "#fff",
          border: `1px solid ${panelBorder}`,
          padding: "24px 40px", maxWidth: 480,
        }}>
          <div style={{ fontSize: 32, marginBottom: 8 }}>🌐</div>
          <div style={{ fontWeight: 600, fontSize: 18, marginBottom: 4, color: text }}>
            {currentTitle}
          </div>
          <div style={{ fontSize: 13, color: dim, marginBottom: 16 }}>{currentUrl}</div>
          <div style={{ fontSize: 12, color: dim, lineHeight: 1.7 }}>
            Press <kbd style={{
              background: dark ? "#3a3b46" : "#f1f3f4",
              border: `1px solid ${panelBorder}`,
              borderRadius: 3, padding: "1px 5px", fontSize: 11,
            }}>Ctrl+D</kbd> to bookmark this page
            <br />
            Click any bookmark bar item to navigate
            <br />
            Right-click a bookmark to remove it
            <br />
            Click 🔖 to open the bookmarks panel
          </div>
        </div>

        {/* Quick demo links */}
        <div style={{ marginTop: 20, display: "flex", gap: 8, justifyContent: "center", flexWrap: "wrap" }}>
          {[
            { title: "Google",    url: "https://www.google.com" },
            { title: "Reddit",    url: "https://www.reddit.com" },
            { title: "Twitter",   url: "https://twitter.com" },
            { title: "Stack Overflow", url: "https://stackoverflow.com" },
          ].map(link => (
            <button
              key={link.url}
              onClick={() => navigateTo(link.url, link.title)}
              style={{
                background: dark ? "#2e2f3a" : "#e8eaed",
                border: "none", borderRadius: 20,
                padding: "6px 14px", cursor: "pointer",
                fontSize: 12, color: text,
                transition: "background .15s",
              }}
              onMouseEnter={e => e.currentTarget.style.background = hover}
              onMouseLeave={e => e.currentTarget.style.background = dark ? "#2e2f3a" : "#e8eaed"}
            >
              {link.title}
            </button>
          ))}
        </div>
      </div>

      {/* ── BOOKMARK SIDE PANEL — matches C++ DrawBookmarkPanel() ── */}
      {panelOpen && (
        <div
          ref={panelRef}
          style={{
            position: "fixed", top: 0, right: 0, width: 340,
            height: "100vh", background: panel,
            borderLeft: `1px solid ${panelBorder}`,
            boxShadow: "-4px 0 20px rgba(0,0,0,0.18)",
            display: "flex", flexDirection: "column",
            zIndex: 200, transition: "transform .2s",
          }}
        >
          {/* Header */}
          <div style={{
            background: panelHeader,
            borderBottom: `1px solid ${panelBorder}`,
            padding: "14px 16px",
            display: "flex", alignItems: "center", gap: 10,
          }}>
            <span style={{ fontSize: 18, color: TEAL }}>🔖</span>
            <span style={{ fontWeight: 600, fontSize: 15, flex: 1, color: text }}>Bookmarks</span>
            <button
              onClick={() => setPanelOpen(false)}
              style={{
                background: "none", border: "none", cursor: "pointer",
                color: dim, fontSize: 16, padding: "2px 6px", borderRadius: 4,
                transition: "background .15s, color .15s",
              }}
              onMouseEnter={e => { e.target.style.color="#e03030"; e.target.style.background=dark?"rgba(220,50,50,.12)":"rgba(220,50,50,.08)"; }}
              onMouseLeave={e => { e.target.style.color=dim; e.target.style.background="none"; }}
            >✕</button>
          </div>

          {/* Count */}
          <div style={{ padding: "8px 16px 4px", fontSize: 11, color: dim }}>
            {bookmarks.length} bookmark{bookmarks.length !== 1 ? "s" : ""}
          </div>

          {/* List */}
          <div style={{ flex: 1, overflowY: "auto", padding: "4px 0" }}>
            {bookmarks.length === 0 ? (
              <div style={{ padding: "20px 20px", fontSize: 13, color: dim, lineHeight: 1.6 }}>
                No bookmarks yet.<br />
                Press <strong>Ctrl+D</strong> to add the current page.
              </div>
            ) : (
              bookmarks.map((b, i) => (
                <BookmarkItem
                  key={i}
                  b={b}
                  i={i}
                  dark={dark}
                  text={text}
                  dim={dim}
                  hover={hover}
                  panelBorder={panelBorder}
                  onNavigate={() => navigateTo(b.url, b.title)}
                  onRemove={() => { removeBookmark(i); showToast("Bookmark removed","remove"); }}
                />
              ))
            )}
          </div>
        </div>
      )}

      {/* ── Ctrl+D ADD-BOOKMARK DIALOG (Chrome-style) ── */}
      {addDialogOpen && (
        <div style={{
          position: "fixed", top: 0, left: 0, width: "100vw", height: "100vh",
          background: "rgba(0,0,0,0.25)", zIndex: 300,
          display: "flex", alignItems: "flex-start", justifyContent: "center",
          paddingTop: 80,
        }}>
          <div style={{
            background: panel, borderRadius: 10,
            border: `1px solid ${panelBorder}`,
            boxShadow: "0 8px 32px rgba(0,0,0,.28)",
            width: 340, padding: 20,
          }}>
            <div style={{ fontWeight: 600, fontSize: 15, marginBottom: 14, color: text }}>
              {isBookmarked(dialogUrl) ? "Edit bookmark" : "Bookmark added"}
            </div>

            <label style={{ fontSize: 12, color: dim, display: "block", marginBottom: 4 }}>Name</label>
            <input
              autoFocus
              value={dialogTitle}
              onChange={e => setDialogTitle(e.target.value)}
              style={{
                width: "100%", boxSizing: "border-box",
                background: inputBg, border: `1px solid ${panelBorder}`,
                borderRadius: 5, padding: "7px 10px",
                color: text, fontSize: 13, outline: "none", marginBottom: 10,
              }}
            />

            <label style={{ fontSize: 12, color: dim, display: "block", marginBottom: 4 }}>URL</label>
            <input
              value={dialogUrl}
              onChange={e => setDialogUrl(e.target.value)}
              style={{
                width: "100%", boxSizing: "border-box",
                background: inputBg, border: `1px solid ${panelBorder}`,
                borderRadius: 5, padding: "7px 10px",
                color: text, fontSize: 13, outline: "none", marginBottom: 16,
              }}
            />

            <div style={{ display: "flex", gap: 8, justifyContent: "flex-end" }}>
              {isBookmarked(dialogUrl) && (
                <button
                  onClick={() => {
                    const idx = bookmarks.findIndex(b => b.url === dialogUrl);
                    if (idx !== -1) removeBookmark(idx);
                    showToast("Bookmark removed", "remove");
                    setAddDialogOpen(false);
                  }}
                  style={{
                    background: "none", border: `1px solid ${panelBorder}`,
                    borderRadius: 5, padding: "7px 14px", cursor: "pointer",
                    color: "#e03030", fontSize: 13, marginRight: "auto",
                  }}
                >Remove</button>
              )}
              <button
                onClick={() => setAddDialogOpen(false)}
                style={{
                  background: "none", border: `1px solid ${panelBorder}`,
                  borderRadius: 5, padding: "7px 14px", cursor: "pointer",
                  color: text, fontSize: 13,
                }}
              >Cancel</button>
              <button
                onClick={confirmAdd}
                style={{
                  background: TEAL, border: "none", borderRadius: 5,
                  padding: "7px 18px", cursor: "pointer",
                  color: "#fff", fontSize: 13, fontWeight: 600,
                }}
              >
                {isBookmarked(dialogUrl) ? "Save" : "Done"}
              </button>
            </div>
          </div>
        </div>
      )}

      {/* ── Toast notification ── */}
      {toast && (
        <div style={{
          position: "fixed", bottom: 32, left: "50%",
          transform: "translateX(-50%)",
          background: toast.type === "remove" ? "#c0392b" : toast.type === "nav" ? "#2980b9" : TEAL,
          color: "#fff", borderRadius: 20,
          padding: "8px 20px", fontSize: 13,
          boxShadow: "0 4px 16px rgba(0,0,0,.25)",
          zIndex: 400, pointerEvents: "none",
          animation: "fadein .2s",
        }}>
          {toast.msg}
        </div>
      )}

      <style>{`
        @keyframes fadein { from { opacity:0; transform:translateX(-50%) translateY(8px); } to { opacity:1; transform:translateX(-50%) translateY(0); } }
        ::-webkit-scrollbar { width: 4px; height: 4px; }
        ::-webkit-scrollbar-thumb { background: rgba(128,128,160,.3); border-radius: 4px; }
      `}</style>
    </div>
  );
}

// ── Individual bookmark item in the side panel ──
function BookmarkItem({ b, i, dark, text, dim, hover, panelBorder, onNavigate, onRemove }) {
  const [hovered, setHovered] = useState(false);

  return (
    <div
      onClick={onNavigate}
      onMouseEnter={() => setHovered(true)}
      onMouseLeave={() => setHovered(false)}
      style={{
        display: "flex", alignItems: "center", gap: 10,
        padding: "10px 16px",
        background: hovered ? hover : "none",
        cursor: "pointer",
        borderBottom: `1px solid ${dark ? "#2d2e38" : "#eaecf0"}`,
        transition: "background .1s",
      }}
    >
      {/* Favicon circle with first letter — matches C++ */}
      <div style={{
        width: 32, height: 32, borderRadius: "50%", flexShrink: 0,
        background: dark ? "#383949" : "#e4e8f0",
        display: "flex", alignItems: "center", justifyContent: "center",
        fontWeight: 700, fontSize: 14, color: "#0CA8B0",
      }}>
        {(b.title || "?")[0].toUpperCase()}
      </div>

      {/* Title + URL */}
      <div style={{ flex: 1, overflow: "hidden" }}>
        <div style={{
          fontSize: 13, color: text, fontWeight: 500,
          whiteSpace: "nowrap", overflow: "hidden", textOverflow: "ellipsis",
        }}>{b.title}</div>
        <div style={{
          fontSize: 11, color: dim,
          whiteSpace: "nowrap", overflow: "hidden", textOverflow: "ellipsis",
        }}>{b.url}</div>
      </div>

      {/* Remove button — visible on hover, matches C++ delete button logic */}
      {hovered && (
        <button
          onClick={e => { e.stopPropagation(); onRemove(); }}
          title="Remove bookmark"
          style={{
            background: "none", border: "none", cursor: "pointer",
            color: dim, fontSize: 14, padding: "2px 4px", borderRadius: 4,
            flexShrink: 0, transition: "color .15s",
          }}
          onMouseEnter={e => e.target.style.color = "#e03030"}
          onMouseLeave={e => e.target.style.color = dim}
        >✕</button>
      )}
    </div>
  );
}
