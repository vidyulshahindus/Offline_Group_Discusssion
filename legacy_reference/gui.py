import tkinter as tk
from tkinter import messagebox
from PIL import Image, ImageTk
import customtkinter
import time
import math
import threading  

class ImprovedGUI:
    def __init__(self, root, start_recording, stop_recording, submit_transcription, exit_program):
        self.root = root
        self.root.title("Eagle App")
        
        # Open the window in full screen
        self.root.attributes("-fullscreen", True)

        # Optionally, if you want an exit full screen shortcut (for example, F11)
        self.root.bind("<F11>", self.toggle_fullscreen)

        # Store the initial fullscreen state
        self.fullscreen = True

        # Timer Variables
        self.timer_running = False
        self.seconds_elapsed = 0

        # Follow-up question toggle state (default: OFF)
        self.follow_up_enabled = False

        # Word limit (default: 75 words)
        self.word_limit = 75

        # Interaction mode: Discussion (current) or Conversational (casual)
        self.interaction_mode = "Discussion"

        # Model: Legacy (gpt-4o-mini chat) or Real Time (Responses + web search)
        self.model_selection = "Legacy"

        # Language policy: English / Hindi (locked) or Multi Language (auto-detect)
        self.language_mode = "English"

        # Auto-detected discussion language (default: English)
        self.detected_language = "English"
        self.discussion_language = "English"
        self._language_priority = 0
        self._last_stt_language_code = ""
        self.last_submitted_user_text = ""
        self.chat_messages = []  # committed turns: {role, content}
        self.pending_utterance = ""  # current turn — exactly what Whisper detected, submitted as-is
        self.conversation_state = None  # modes.conversation_state.ConversationState

        self.start_recording = start_recording
        self.stop_recording = stop_recording
        self.submit_transcription = submit_transcription
        self.exit_program = exit_program

        self.create_layout()
        self.setup_text_tags()
        self.setup_shortcuts()

        # Override close event
        self.root.protocol("WM_DELETE_WINDOW", self.confirm_exit)

    def toggle_fullscreen(self, event=None):
        """Toggle full screen mode."""
        self.fullscreen = not self.fullscreen
        self.root.attributes("-fullscreen", self.fullscreen)
    
    def set_activity_status(self, status):
        """Show Eagle's current state: Ready / Listening / Thinking / Speaking."""
        colors = {
            "Ready": "#27AE60",
            "Listening…": "#E67E22",
            "Thinking…": "#2980B9",  # blue — matches "Eagle is thinking" status line
            "Speaking…": "#2980B9",
        }
        self.activity_status_label.configure(
            text=status,
            fg=colors.get(status, "#1E1E1E"),
        )

    def minimize_window(self):
        """Minimize the window to taskbar."""
        self.root.iconify()  # Minimize the window


    def create_layout(self):
        """Create UI layout with a modern look and a top bar for the timer and instructions."""
        
        # Create a top frame that spans the full width for the timer and instructions button.
        top_frame = tk.Frame(self.root, bg="#E8EDF2")
        top_frame.pack(fill=tk.X, side=tk.TOP, padx=20, pady=10)

        # Timer Label on the left side of the top frame.
        self.timer_label = tk.Label(
            top_frame,
            text="00:00",
            font=("Arial", 14, "bold"),
            fg="#8B4513",
            bg="#E8EDF2"
        )
        self.timer_label.pack(side=tk.LEFT, anchor="w")

        self.activity_status_label = tk.Label(
            top_frame,
            text="Ready",
            font=("Arial", 12, "bold"),
            fg="#27AE60",
            bg="#E8EDF2"
        )
        self.activity_status_label.pack(side=tk.LEFT, padx=(15, 0))

        # Minimize button
        self.minimize_btn = customtkinter.CTkButton(
            top_frame,
            text="−",  # Minus symbol for minimize
            command=self.minimize_window,
            font=("Arial", 16, "bold"),
            fg_color="#95A5A6",    # Gray color
            corner_radius=10,
            hover_color="#7F8C8D", # Darker gray on hover
            width=40,
            height=30
        )
        self.minimize_btn.pack(side=tk.RIGHT, anchor="e", padx=(0, 10))

        # "How to use ?" button on the right side of the top frame.
        self.how_to_use_btn = customtkinter.CTkButton(
            top_frame,
            text="How to use ?",
            command=self.show_instructions,
            font=("Arial", 12, "bold"),
            fg_color="#3498DB",    # Blue color
            corner_radius=10,
            hover_color="#5DADE2", # Lighter blue on hover
            width=80,
            height=30
        )
        self.how_to_use_btn.pack(side=tk.RIGHT, anchor="e")

        # Generate Speech popup (next to How to use)
        self.generate_speech_btn = customtkinter.CTkButton(
            top_frame,
            text="Generate Speech",
            command=self.show_speech_studio,
            font=("Arial", 12, "bold"),
            fg_color="#8E44AD",
            corner_radius=10,
            hover_color="#9B59B6",
            width=130,
            height=30,
        )
        self.generate_speech_btn.pack(side=tk.RIGHT, anchor="e", padx=(0, 8))

        # The rest of the content goes into a container frame.
        container = tk.Frame(self.root, bg="#E8EDF2")
        container.pack(fill=tk.BOTH, expand=True, padx=20, pady=(0, 20))

        # Logo and Heading
        logo_frame = tk.Frame(container, bg="#E8EDF2")
        logo_frame.pack(pady=10)

        try:
            self.logo_img = Image.open("images/logo1.png")  # Ensure logo is present
            self.logo_img = self.logo_img.resize((120, 80), Image.LANCZOS)
            self.logo_photo = ImageTk.PhotoImage(self.logo_img)
            logo_label = tk.Label(logo_frame, image=self.logo_photo, bg="#E8EDF2")
            logo_label.pack()
        except Exception as e:
            print("Error loading logo:", e)

        heading_label = tk.Label(
            logo_frame,
            text="Gen AI Group Discussion",
            font=("Arial", 20, "bold"),
            fg="#1E1E1E",
            bg="#E8EDF2"
        )
        heading_label.pack(pady=5)

        # Conversation Display Box
        display_frame = tk.Frame(container, bg="white")
        display_frame.pack(expand=True, fill=tk.BOTH, padx=20, pady=10)

        self.dialog_box = tk.Text(
            display_frame,
            height=15,
            width=70,
            bg="#FDFDFD",
            font=("Helvetica", 12),
            padx=15,
            pady=10,
            wrap=tk.WORD,
            bd=0,             # Remove default border
            relief="flat",    # Use flat relief to remove any extra edge
            highlightthickness=2,
            highlightbackground="blue",
            highlightcolor="blue"
        )
        self.dialog_box.pack(fill=tk.BOTH, expand=True)

        # Follow-Up Question Box (new)
        follow_up_frame = tk.Frame(container, bg="white")
        follow_up_frame.pack(fill=tk.BOTH, padx=20, pady=(0, 10))

        # Header frame for label and toggle
        follow_up_header = tk.Frame(follow_up_frame, bg="white")
        follow_up_header.pack(fill=tk.X, padx=10, pady=(5, 0))

        follow_up_label = tk.Label(
            follow_up_header,
            text="Follow-Up Question:",
            font=("Helvetica", 14, "bold"),
            fg="#1E1E1E",
            bg="white"
        )
        follow_up_label.pack(side=tk.LEFT, anchor="w")

        # Right side controls frame
        right_controls = tk.Frame(follow_up_header, bg="white")
        right_controls.pack(side=tk.RIGHT)

        # Auto language indicator (or Locked when English/Hindi selected)
        self.language_status_label = tk.Label(
            right_controls,
            text="Locked: English",
            font=("Helvetica", 11, "bold"),
            fg="#2980B9",
            bg="white"
        )
        self.language_status_label.pack(side=tk.LEFT, padx=(0, 15))

        # Mode dropdown — Discussion vs Conversational
        mode_label = tk.Label(
            right_controls,
            text="Mode:",
            font=("Helvetica", 12),
            fg="#1E1E1E",
            bg="white"
        )
        mode_label.pack(side=tk.LEFT, padx=(0, 5))

        self.mode_var = tk.StringVar(value="Discussion")
        self.mode_dropdown = customtkinter.CTkComboBox(
            right_controls,
            values=["Discussion", "Conversational"],
            variable=self.mode_var,
            command=self.on_mode_change,
            font=("Arial", 11, "bold"),
            width=130,
            height=30,
            dropdown_font=("Arial", 11),
            fg_color="#FFFFFF",
            border_color="#9B59B6",
            button_color="#9B59B6",
            button_hover_color="#8E44AD"
        )
        self.mode_dropdown.pack(side=tk.LEFT, padx=(0, 15))

        # Language policy dropdown — English / Hindi lock, or Multi Language (auto)
        language_label = tk.Label(
            right_controls,
            text="Language:",
            font=("Helvetica", 12),
            fg="#1E1E1E",
            bg="white",
        )
        language_label.pack(side=tk.LEFT, padx=(0, 5))

        self.language_mode_var = tk.StringVar(value="English")
        self.language_mode_dropdown = customtkinter.CTkComboBox(
            right_controls,
            values=["English", "Hindi", "Multi Language"],
            variable=self.language_mode_var,
            command=self.on_language_mode_change,
            font=("Arial", 11, "bold"),
            width=130,
            height=30,
            dropdown_font=("Arial", 11),
            fg_color="#FFFFFF",
            border_color="#16A085",
            button_color="#16A085",
            button_hover_color="#138D75",
        )
        self.language_mode_dropdown.pack(side=tk.LEFT, padx=(0, 15))

        # Model dropdown — Legacy vs Real Time (web search)
        model_label = tk.Label(
            right_controls,
            text="Model:",
            font=("Helvetica", 12),
            fg="#1E1E1E",
            bg="white",
        )
        model_label.pack(side=tk.LEFT, padx=(0, 5))

        self.model_var = tk.StringVar(value="Legacy")
        self.model_dropdown = customtkinter.CTkComboBox(
            right_controls,
            values=["Legacy", "Real Time"],
            variable=self.model_var,
            command=self.on_model_selection_change,
            font=("Arial", 11, "bold"),
            width=110,
            height=30,
            dropdown_font=("Arial", 11),
            fg_color="#FFFFFF",
            border_color="#E67E22",
            button_color="#E67E22",
            button_hover_color="#D35400",
        )
        self.model_dropdown.pack(side=tk.LEFT, padx=(0, 15))

        # Word limit dropdown
        word_limit_label = tk.Label(
            right_controls,
            text="Word Limit:",
            font=("Helvetica", 12),
            fg="#1E1E1E",
            bg="white"
        )
        word_limit_label.pack(side=tk.LEFT, padx=(0, 5))

        self.word_limit_var = tk.StringVar(value="75")
        word_limit_options = [str(i) for i in range(50, 201, 5)]  # 50, 55, … 75 … 200
        self.word_limit_dropdown = customtkinter.CTkComboBox(
            right_controls,
            values=word_limit_options,
            variable=self.word_limit_var,
            command=self.on_word_limit_change,
            font=("Arial", 11, "bold"),
            width=80,
            height=30,
            dropdown_font=("Arial", 11),
            fg_color="#FFFFFF",
            border_color="#3498DB",
            button_color="#3498DB",
            button_hover_color="#2980B9"
        )
        self.word_limit_dropdown.pack(side=tk.LEFT, padx=(0, 15))

        # Toggle switch for follow-up questions
        toggle_label = tk.Label(
            right_controls,
            text="Enable:",
            font=("Helvetica", 12),
            fg="#1E1E1E",
            bg="white"
        )
        toggle_label.pack(side=tk.LEFT, padx=(0, 5))

        self.follow_up_toggle = customtkinter.CTkSwitch(
            right_controls,
            text="ON" if self.follow_up_enabled else "OFF",
            command=self.toggle_follow_up,
            font=("Arial", 12, "bold"),
            fg_color="#27AE60" if self.follow_up_enabled else "#95A5A6",
            progress_color="#27AE60",
            button_color="#FFFFFF",
            button_hover_color="#ECF0F1"
        )
        self.follow_up_toggle.pack(side=tk.LEFT)
        if self.follow_up_enabled:
            self.follow_up_toggle.select()

        self.follow_up_box = tk.Text(
            follow_up_frame,
            height=3,
            width=70,
            bg="#FDFDFD",
            font=("Helvetica", 12),
            padx=15,
            pady=10,
            wrap=tk.WORD,
            bd=0,             # Remove default border
            relief="flat",    # Use flat relief
            highlightthickness=2,
            highlightbackground="blue",
            highlightcolor="blue"
        )
        self.follow_up_box.pack(fill=tk.BOTH, expand=True, padx=10, pady=5)

        # Button Bar at the bottom
        button_frame = tk.Frame(container, bg="#E8EDF2")
        button_frame.pack(pady=15)

        buttons = [
            ("Start (Ctrl + R)", self.start_timer, "#3498DB"),
            ("Stop (Ctrl + S)", self.stop_timer, "#3498DB"),
            ("Submit (Enter)", self.submit_transcription, "#2ECC71"),
            ("Exit (Esc)", self.confirm_exit, "#E74C3C")
        ]

        for text, command, color in buttons:
            customtkinter.CTkButton(
                button_frame,
                text=text,
                command=command,
                font=("Arial", 14, "bold"),
                fg_color=color,
                corner_radius=25,
                hover_color="#BDC3C7",
                width=190,
                height=50
            ).pack(side=tk.LEFT, padx=15)



    def setup_text_tags(self):
        """Set text colors for messages."""
        self.dialog_box.tag_configure("status", foreground="blue")
        self.dialog_box.tag_configure("user", foreground="orange")
        self.dialog_box.tag_configure("assistant", foreground="green")
        self.dialog_box.tag_configure("follow_up", foreground="red")
        self.dialog_box.tag_configure("draft", foreground="#CC7700")
        self.dialog_box.tag_configure("whisper_interim", foreground="#CC7700")
        self.dialog_box.tag_configure("hist_user", foreground="orange")
        self.dialog_box.tag_configure("hist_assistant", foreground="green")
        # Configure tags for follow-up box
        self.follow_up_box.tag_configure("follow_up", foreground="red")
        self.follow_up_box.tag_configure("status", foreground="blue")

    def setup_shortcuts(self):
        """Keyboard shortcuts for buttons."""
        self.root.bind("<Control-r>", lambda event: self.start_timer())
        self.root.bind("<Control-s>", lambda event: self.stop_timer())
        self.root.bind("<Return>", lambda event: self.submit_transcription())
        self.root.bind("<Escape>", lambda event: self.confirm_exit())

    def start_timer(self):
        """Start the timer when 'Start' is clicked."""
        if not self.timer_running:
            self.timer_running = True
            self.seconds_elapsed = 0
            self.update_timer()

        if self.start_recording:
            self.start_recording()

    def update_timer(self):
        """Update the timer label every second."""
        if self.timer_running:
            minutes = self.seconds_elapsed // 60
            seconds = self.seconds_elapsed % 60
            self.timer_label.config(text=f"{minutes:02}:{seconds:02}")
            self.seconds_elapsed += 1
            self.root.after(1000, self.update_timer)

    def stop_timer(self):
        """Stop the timer when 'Stop' is clicked."""
        self.timer_running = False

        if self.stop_recording:
            self.stop_recording()

    def on_mode_change(self, value):
        """Handle Discussion / Conversational mode switch."""
        self.interaction_mode = value
        self.update_language_display()
        print(f"Mode changed to: {value}")

    def on_language_mode_change(self, value):
        """English/Hindi = locked response language; Multi Language = auto-detect."""
        self.language_mode = value
        if value in ("English", "Hindi"):
            self.detected_language = value
            self.discussion_language = value
            self._language_priority = 999
            wt = getattr(self, "whisper_transcriber", None)
            if wt:
                wt._ui_language_lock = value
                try:
                    wt.get_dominant_tracker().force_lock(value)
                except Exception:
                    pass
        else:
            # Multi Language — clear frozen English lock so Telugu/Hindi can be detected
            self._language_priority = 0
            wt = getattr(self, "whisper_transcriber", None)
            if wt:
                wt._ui_language_lock = None
                try:
                    wt.get_dominant_tracker().reset()
                except Exception:
                    pass
            lock = getattr(self, "language_lock", None)
            if lock:
                try:
                    lock.reset()
                except Exception:
                    pass
            self._stt_restart_requested = True
        self.update_language_display()
        print(f"Language mode changed to: {value}")

    def on_model_selection_change(self, value):
        """Legacy = gpt-4o-mini chat; Real Time = GPT + web search."""
        self.model_selection = value
        print(f"Model selection changed to: {value}")

    def on_word_limit_change(self, value):
        """Handle word limit dropdown change."""
        self.word_limit = int(value)
        print(f"Word limit changed to: {self.word_limit} words")

    def update_language_display(self):
        """Update auto-detected / locked language label in the GUI."""
        policy = getattr(self, "language_mode", "English")
        lang = getattr(self, "detected_language", "English")
        if policy in ("English", "Hindi"):
            self.language_status_label.configure(text=f"Locked: {policy}")
            return
        wt = getattr(self, "whisper_transcriber", None)
        locked = (wt and wt.get_locked_language()) or (
            getattr(self, "language_lock", None)
            and self.language_lock.is_enabled()
            and self.language_lock.is_locked()
        )
        if locked:
            self.language_status_label.configure(text=f"Auto: {lang} ✓")
        else:
            self.language_status_label.configure(text=f"Auto: {lang} …")

    def toggle_follow_up(self):
        """Toggle follow-up question generation on/off."""
        self.follow_up_enabled = self.follow_up_toggle.get()
        if self.follow_up_enabled:
            self.follow_up_toggle.configure(text="ON", fg_color="#27AE60")
            # Clear the disabled message if it exists
            if "Follow-up question is disabled" in self.follow_up_box.get("1.0", tk.END):
                self.follow_up_box.delete("1.0", tk.END)
        else:
            self.follow_up_toggle.configure(text="OFF", fg_color="#95A5A6")
            # Show disabled message in follow-up box
            self.follow_up_box.delete("1.0", tk.END)
            self.follow_up_box.insert("1.0", "Follow-up question is disabled", "status")
        status = "enabled" if self.follow_up_enabled else "disabled"
        print(f"Follow-up questions {status}")

    def show_loading_overlay(self):
        """Show a simple text message in the center of the screen."""
        # Create a simple overlay window
        self.loading_overlay = tk.Toplevel(self.root)
        self.loading_overlay.attributes("-topmost", True)
        self.loading_overlay.configure(bg="#E8EDF2")  # Same background as main window
        
        # Get screen dimensions and center the window
        screen_width = self.loading_overlay.winfo_screenwidth()
        screen_height = self.loading_overlay.winfo_screenheight()
        window_width = 400
        window_height = 100
        x = (screen_width // 2) - (window_width // 2)
        y = (screen_height // 2) - (window_height // 2)
        self.loading_overlay.geometry(f"{window_width}x{window_height}+{x}+{y}")
        
        # Remove window decorations
        self.loading_overlay.overrideredirect(True)
        
        # Ensure overlay stays on top
        self.loading_overlay.lift()
        
        # Loading text centered - normal style like dialog text
        loading_label = tk.Label(
            self.loading_overlay,
            text="Closing application please wait....",
            font=("Arial", 12),  # Normal font size, not bold
            fg="#1E1E1E",  # Dark text color like normal dialog
            bg="#E8EDF2"  # Same background as main window
        )
        loading_label.place(relx=0.5, rely=0.5, anchor="center")

    def confirm_exit(self, event=None):
        """Confirm exit before closing."""
        response = messagebox.askyesno("Exit Confirmation", "Are you sure you want to exit the discussion?")
        if response:
            # Show loading overlay immediately
            self.show_loading_overlay()
            # Update the window to ensure overlay is visible
            self.root.update()
            
            # Stop timer
            self.timer_running = False
            
            # Call exit_program callback which will close everything
            if self.exit_program:
                # Run exit in a separate thread to allow loader to remain visible
                def delayed_exit():
                    time.sleep(0.5)  # Small delay to ensure loader is visible
                    self.exit_program()
                threading.Thread(target=delayed_exit, daemon=False).start()
            else:
                # Fallback: quit after a short delay
                self.root.after(500, self.root.quit)

    def show_speech_studio(self):
        """Popup: paste text → language → generate / save / replay speech."""
        try:
            from speech_studio import open_speech_studio
            open_speech_studio(self.root)
        except Exception as e:
            messagebox.showerror("Generate Speech", f"Could not open: {e}")

    def show_instructions(self):
        """Display instructions in a modern dialog."""
        instr_win = tk.Toplevel(self.root)
        instr_win.title("How to use ?")
        instr_win.configure(bg="#FDFDFD")
        win_width = 500
        win_height = 300
        screen_width = instr_win.winfo_screenwidth()
        screen_height = instr_win.winfo_screenheight()
        x_coordinate = (screen_width // 2) - (win_width // 2)
        y_coordinate = (screen_height // 2) - (win_height // 2)
        instr_win.geometry(f"{win_width}x{win_height}+{x_coordinate}+{y_coordinate}")

        content_frame = customtkinter.CTkFrame(instr_win, width=win_width-40, height=win_height-100, fg_color="white", corner_radius=15)
        content_frame.place(relx=0.5, rely=0.4, anchor="center")

        instructions = (
            "- Press Start to initiate the discussion.\n"
            "- Give critical thinking and thought-provoking prompts.\n"
            "- Once prompts are completed, press the Submit button.\n"
            "- Wait for the response and follow-up question.\n"
            "- Continue the discussion thereafter.\n"
        )

        instruction_label = customtkinter.CTkLabel(
            content_frame,
            text=instructions,
            font=("Helvetica", 14, "bold"), 
            text_color="#1E1E1E",  
            fg_color="transparent",
            justify="left"
        )
        instruction_label.pack(padx=20, pady=20, fill="both", expand=True)

        ok_button = customtkinter.CTkButton(
            instr_win,
            text="OK",
            command=instr_win.destroy,
            font=("Arial", 12, "bold"),
            fg_color="#E74C3C",
            corner_radius=10,
            hover_color="#F1948A",
            width=100,
            height=40
        )
        ok_button.place(relx=0.5, rely=0.85, anchor="center")

        instr_win.grab_set()


if __name__ == "__main__":
    root = customtkinter.CTk()
    app = ImprovedGUI(root, None, None, None, None)
    root.mainloop()
