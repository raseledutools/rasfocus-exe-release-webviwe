document.addEventListener('DOMContentLoaded', () => {
    // Tab switching logic
    const navItems = document.querySelectorAll('.nav-item');
    const tabContents = document.querySelectorAll('.tab-content');

    navItems.forEach(item => {
        item.addEventListener('click', () => {
            // Remove active class from all items and contents
            navItems.forEach(nav => nav.classList.remove('active'));
            tabContents.forEach(tab => tab.classList.remove('active'));

            // Add active class to clicked item
            item.classList.add('active');

            // Show corresponding tab content
            const tabId = item.getAttribute('data-tab');
            const content = document.getElementById(`tab-${tabId}`);
            if (content) {
                content.classList.add('active');
            }
        });
    });

    // Initialize backend connection (simulated for now until Tauri builds)
    initializeBackend();
});

// Helper function to call Rust backend using Tauri
async function invokeRust(command, args = {}) {
    try {
        if (window.__TAURI__) {
            const { invoke } = window.__TAURI__.tauri;
            console.log(`Invoking Rust command: ${command}`);
            return await invoke(command, args);
        } else {
            console.warn('Tauri API not found. Are you running in Tauri?');
            // Fallback for browser testing
            return handleMockCommand(command, args);
        }
    } catch (error) {
        console.error(`Error executing ${command}:`, error);
        throw error;
    }
}

async function initializeBackend() {
    try {
        // Example: Fetch user details from Rust backend
        const greeting = document.getElementById('user-greeting');
        const user = await invokeRust('get_user_info');
        if (user && greeting) {
            greeting.textContent = `Welcome back, ${user.name}`;
        }
    } catch (error) {
        console.log("Using default UI states");
    }
}

function handleMockCommand(command, args) {
    switch (command) {
        case 'toggle_adult_filter':
            alert('Adult filter toggled (Mock)');
            return true;
        case 'kill_debug_apps':
            alert('Killed debug apps like Taskmgr (Mock)');
            return true;
        case 'toggle_internet':
            alert(`Internet Block set to: ${args} (Mock)`);
            return true;
        case 'toggle_install':
            alert(`Install Block set to: ${args} (Mock)`);
            return true;
        case 'toggle_audio':
            alert(`Ambient Noise set to: ${args} (Mock)`);
            return true;
        case 'connect_parent':
            alert('Connecting to Parent Device... (Mock)');
            return true;
        case 'open_pdf_reader':
            alert('Opening PDF Reader... (Mock)');
            return true;
        case 'connect_remote':
            alert('Connecting to Remote Device... (Mock)');
            return true;
        case 'get_user_info':
            return { name: 'Admin', isPremium: true };
        default:
            return null;
    }
}

// Phase 7: Deep Study Timer Logic
let dsTimer = null;
let dsTimeLeft = 0;
let dsIsFocus = true;
let dsCurrentSession = 1;
let dsTotalSessions = 4;
let dsFocusTime = 25;
let dsBreakTime = 5;

function startDeepStudy() {
    if (dsTimer) {
        // Stop timer
        clearInterval(dsTimer);
        dsTimer = null;
        document.getElementById('ds-inputs').style.display = 'flex';
        document.getElementById('ds-display').style.display = 'none';
        document.getElementById('ds-btn-text').innerText = 'Start Deep Study';
        document.getElementById('ds-start-btn').classList.replace('btn-kill', 'btn-primary');
        return;
    }

    // Start timer
    dsFocusTime = parseInt(document.getElementById('ds-focus').value) || 25;
    dsBreakTime = parseInt(document.getElementById('ds-break').value) || 5;
    dsTotalSessions = parseInt(document.getElementById('ds-sessions').value) || 4;
    
    dsCurrentSession = 1;
    dsIsFocus = true;
    dsTimeLeft = dsFocusTime * 60;

    document.getElementById('ds-inputs').style.display = 'none';
    document.getElementById('ds-display').style.display = 'block';
    document.getElementById('ds-btn-text').innerText = 'Stop Session';
    document.getElementById('ds-start-btn').classList.replace('btn-primary', 'btn-kill');

    updateDsDisplay();
    dsTimer = setInterval(tickDeepStudy, 1000);
}

function tickDeepStudy() {
    if (dsTimeLeft > 0) {
        dsTimeLeft--;
        updateDsDisplay();
    } else {
        // Time is up, switch mode
        if (dsIsFocus) {
            if (dsCurrentSession >= dsTotalSessions) {
                // Done with all sessions
                clearInterval(dsTimer);
                dsTimer = null;
                alert('Deep Study Complete! Great job!');
                startDeepStudy(); // reset UI
                return;
            }
            dsIsFocus = false;
            dsTimeLeft = dsBreakTime * 60;
            // Play a ding sound here in real app
        } else {
            dsIsFocus = true;
            dsCurrentSession++;
            dsTimeLeft = dsFocusTime * 60;
        }
        updateDsDisplay();
    }
}

function updateDsDisplay() {
    const m = Math.floor(dsTimeLeft / 60).toString().padStart(2, '0');
    const s = (dsTimeLeft % 60).toString().padStart(2, '0');
    document.getElementById('ds-time').innerText = ${m}:;
    
    const statusEl = document.getElementById('ds-status');
    const timeEl = document.getElementById('ds-time');
    
    if (dsIsFocus) {
        statusEl.innerText = Focus Session /;
        timeEl.style.color = 'var(--accent-primary)';
    } else {
        statusEl.innerText = Break Time!;
        timeEl.style.color = 'var(--accent-secondary)';
    }
}
