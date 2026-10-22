// Copyright (c) 2026 SpoutUE contributors. MIT licensed.
//
// Windows installer for the precompiled plugin. Built by Build-Installer.ps1 with
// the C# compiler that ships with .NET Framework 4, so it needs nothing beyond
// Windows itself. Kept to C# 5 for that compiler.
//
// One zip per engine version is embedded as a resource named "Payload.<major>.<minor>.zip".
// The plugin goes to <Engine>\Engine\Plugins\Marketplace\SpoutUE, which is where
// Fab installs engine plugins, so it shows up in every project of that engine.
//
// Command line (also used by the build script's self-test):
//   /install | /uninstall      act without showing the window
//   /engine=5.8;5.5            limit to these versions (default: every supported one found)
//   /engineroot=C:\path;...    use these engine folders instead of detecting them
//   /log=C:\path\file.txt      write what happened to a file
//   /noelevate                 do not ask for administrator rights

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Reflection;
using System.Security.Principal;
using System.Text;
using System.Text.RegularExpressions;
using System.Windows.Forms;

namespace SpoutUEInstaller
{
	/// <summary>An Unreal Engine install found on this machine.</summary>
	sealed class EngineInstall
	{
		public string Root;          // C:\Program Files\Epic Games\UE_5.8
		public string Version;       // 5.8.3
		public string MajorMinor;    // 5.8

		public string MarketplaceDir { get { return Path.Combine(Root, "Engine", "Plugins", "Marketplace"); } }
		public string PluginDir { get { return Path.Combine(MarketplaceDir, Program.PluginFolderName); } }

		/// <summary>VersionName of the plugin already installed there, or null.</summary>
		public string InstalledPluginVersion()
		{
			string descriptor = Path.Combine(PluginDir, "SpoutUE.uplugin");
			if (!File.Exists(descriptor))
			{
				return null;
			}
			Match m = Regex.Match(File.ReadAllText(descriptor), "\"VersionName\"\\s*:\\s*\"([^\"]*)\"");
			return m.Success ? m.Groups[1].Value : "?";
		}
	}

	static class Program
	{
		public const string PluginFolderName = "SpoutUE";
		const string PayloadPrefix = "Payload.";
		const string PayloadSuffix = ".zip";

		static StringBuilder Log = new StringBuilder();
		static string LogPath;

		[STAThread]
		static int Main(string[] args)
		{
			Dictionary<string, string> options = ParseArgs(args);
			options.TryGetValue("log", out LogPath);

			// Program Files needs administrator rights. Ask once, up front, unless the
			// caller pointed us at engine folders of its own (tests, source builds).
			if (!IsAdministrator() && !options.ContainsKey("noelevate") && !options.ContainsKey("engineroot"))
			{
				int elevatedExitCode = Relaunch(args);
				if (elevatedExitCode >= 0)
				{
					return elevatedExitCode;
				}
				// UAC refused: carry on, the install will report what it cannot write.
			}

			List<EngineInstall> engines = options.ContainsKey("engineroot")
				? options["engineroot"].Split(';').Where(p => p.Length > 0).Select(DescribeEngine).Where(e => e != null).ToList()
				: DetectEngines();

			if (options.ContainsKey("install") || options.ContainsKey("uninstall"))
			{
				List<EngineInstall> targets = FilterEngines(engines, options);
				bool ok = options.ContainsKey("install") ? Install(targets) : Uninstall(targets);
				FlushLog();
				return ok ? 0 : 1;
			}

			Application.EnableVisualStyles();
			Application.SetCompatibleTextRenderingDefault(false);
			Application.Run(new InstallerForm(engines));
			FlushLog();
			return 0;
		}

		// ---------------------------------------------------------------- payloads

		public static string PluginVersion
		{
			get
			{
				object[] attributes = Assembly.GetExecutingAssembly().GetCustomAttributes(typeof(AssemblyInformationalVersionAttribute), false);
				return attributes.Length > 0 ? ((AssemblyInformationalVersionAttribute)attributes[0]).InformationalVersion : "?";
			}
		}

		/// <summary>Engine versions ("5.8") this installer carries a build for.</summary>
		public static List<string> SupportedVersions()
		{
			return Assembly.GetExecutingAssembly().GetManifestResourceNames()
				.Where(n => n.StartsWith(PayloadPrefix) && n.EndsWith(PayloadSuffix))
				.Select(n => n.Substring(PayloadPrefix.Length, n.Length - PayloadPrefix.Length - PayloadSuffix.Length))
				.OrderBy(v => v)
				.ToList();
		}

		public static bool HasPayload(EngineInstall engine)
		{
			return SupportedVersions().Contains(engine.MajorMinor);
		}

		// ---------------------------------------------------------------- detection

		/// <summary>Launcher installs, plus any UE_5.x folder in the default location.</summary>
		public static List<EngineInstall> DetectEngines()
		{
			var roots = new List<string>();

			string launcherManifest = Path.Combine(
				Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData),
				"Epic", "UnrealEngineLauncher", "LauncherInstalled.dat");
			if (File.Exists(launcherManifest))
			{
				// Each entry carries "InstallLocation" and "AppName"; engines are named UE_x.y.
				foreach (Match m in Regex.Matches(File.ReadAllText(launcherManifest),
					"\"InstallLocation\"\\s*:\\s*\"([^\"]+)\"[^}]*?\"AppName\"\\s*:\\s*\"(UE_[^\"]+)\""))
				{
					roots.Add(m.Groups[1].Value.Replace("\\\\", "\\").Replace("/", "\\"));
				}
			}

			string defaultParent = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "Epic Games");
			if (Directory.Exists(defaultParent))
			{
				roots.AddRange(Directory.GetDirectories(defaultParent, "UE_5.*"));
			}

			return roots
				.Select(r => r.TrimEnd('\\'))
				.Distinct(StringComparer.OrdinalIgnoreCase)
				.Select(DescribeEngine)
				.Where(e => e != null)
				.OrderBy(e => e.MajorMinor)
				.ToList();
		}

		/// <summary>Reads Engine\Build\Build.version; null if this is not an engine.</summary>
		public static EngineInstall DescribeEngine(string root)
		{
			string buildVersion = Path.Combine(root, "Engine", "Build", "Build.version");
			if (!File.Exists(buildVersion))
			{
				return null;
			}

			string text = File.ReadAllText(buildVersion);
			Match major = Regex.Match(text, "\"MajorVersion\"\\s*:\\s*(\\d+)");
			Match minor = Regex.Match(text, "\"MinorVersion\"\\s*:\\s*(\\d+)");
			Match patch = Regex.Match(text, "\"PatchVersion\"\\s*:\\s*(\\d+)");
			if (!major.Success || !minor.Success)
			{
				return null;
			}

			var engine = new EngineInstall();
			engine.Root = root;
			engine.MajorMinor = major.Groups[1].Value + "." + minor.Groups[1].Value;
			engine.Version = engine.MajorMinor + (patch.Success ? "." + patch.Groups[1].Value : "");
			return engine;
		}

		static List<EngineInstall> FilterEngines(List<EngineInstall> engines, Dictionary<string, string> options)
		{
			string wanted;
			if (!options.TryGetValue("engine", out wanted))
			{
				return engines.Where(HasPayload).ToList();
			}
			var versions = new HashSet<string>(wanted.Split(';'));
			return engines.Where(e => versions.Contains(e.MajorMinor)).ToList();
		}

		// ---------------------------------------------------------------- install

		public static bool Install(IEnumerable<EngineInstall> engines)
		{
			bool ok = true;
			foreach (EngineInstall engine in engines)
			{
				ok &= InstallInto(engine);
			}
			return ok;
		}

		public static bool Uninstall(IEnumerable<EngineInstall> engines)
		{
			bool ok = true;
			foreach (EngineInstall engine in engines)
			{
				ok &= UninstallFrom(engine);
			}
			return ok;
		}

		static bool InstallInto(EngineInstall engine)
		{
			string label = "UE " + engine.Version;
			if (!HasPayload(engine))
			{
				Write(label + " : cet installeur ne contient pas de build pour " + engine.MajorMinor + ".");
				return false;
			}
			if (IsEngineRunning(engine))
			{
				Write(label + " : fermez d'abord l'éditeur Unreal de cette version.");
				return false;
			}

			string target = engine.PluginDir;
			string staging = target + ".installing";
			string previous = target + ".previous";

			try
			{
				Directory.CreateDirectory(engine.MarketplaceDir);
				DeleteIfExists(staging);
				DeleteIfExists(previous);

				// Unpacked next to the destination first, so a failure halfway leaves
				// whatever was installed before untouched.
				using (Stream payload = Assembly.GetExecutingAssembly().GetManifestResourceStream(PayloadPrefix + engine.MajorMinor + PayloadSuffix))
				using (var archive = new ZipArchive(payload, ZipArchiveMode.Read))
				{
					archive.ExtractToDirectory(staging);
				}

				if (Directory.Exists(target))
				{
					Directory.Move(target, previous);
				}
				Directory.Move(staging, target);
				DeleteIfExists(previous);

				Write(label + " : Spout UE " + PluginVersion + " installé dans " + target);
				return true;
			}
			catch (UnauthorizedAccessException)
			{
				Write(label + " : accès refusé à " + engine.MarketplaceDir + ". Relancez l'installeur en administrateur.");
			}
			catch (Exception e)
			{
				Write(label + " : échec — " + e.Message);
			}

			// Put the previous install back if the swap did not complete.
			try
			{
				if (!Directory.Exists(target) && Directory.Exists(previous))
				{
					Directory.Move(previous, target);
				}
				DeleteIfExists(staging);
			}
			catch (Exception)
			{
			}
			return false;
		}

		static bool UninstallFrom(EngineInstall engine)
		{
			string label = "UE " + engine.Version;
			if (!Directory.Exists(engine.PluginDir))
			{
				Write(label + " : Spout UE n'est pas installé.");
				return true;
			}
			if (IsEngineRunning(engine))
			{
				Write(label + " : fermez d'abord l'éditeur Unreal de cette version.");
				return false;
			}

			try
			{
				Directory.Delete(engine.PluginDir, true);
				Write(label + " : Spout UE désinstallé.");
				return true;
			}
			catch (UnauthorizedAccessException)
			{
				Write(label + " : accès refusé à " + engine.PluginDir + ". Relancez l'installeur en administrateur.");
			}
			catch (Exception e)
			{
				Write(label + " : échec — " + e.Message);
			}
			return false;
		}

		/// <summary>The plugin DLLs cannot be replaced while that engine's editor has them loaded.</summary>
		public static bool IsEngineRunning(EngineInstall engine)
		{
			string engineBinaries = Path.Combine(engine.Root, "Engine", "Binaries") + "\\";
			foreach (Process process in Process.GetProcessesByName("UnrealEditor").Concat(Process.GetProcessesByName("UnrealEditor-Cmd")))
			{
				try
				{
					if (process.MainModule.FileName.StartsWith(engineBinaries, StringComparison.OrdinalIgnoreCase))
					{
						return true;
					}
				}
				catch (Exception)
				{
					// Another user's or an elevated process: cannot tell, assume not ours.
				}
			}
			return false;
		}

		// ---------------------------------------------------------------- plumbing

		static void DeleteIfExists(string path)
		{
			if (Directory.Exists(path))
			{
				Directory.Delete(path, true);
			}
		}

		public static event Action<string> Logged;

		public static void Write(string line)
		{
			Log.AppendLine(line);
			if (Logged != null)
			{
				Logged(line);
			}
		}

		static void FlushLog()
		{
			if (!string.IsNullOrEmpty(LogPath))
			{
				File.WriteAllText(LogPath, Log.ToString(), Encoding.UTF8);
			}
		}

		static Dictionary<string, string> ParseArgs(string[] args)
		{
			var options = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
			foreach (string arg in args)
			{
				string trimmed = arg.TrimStart('/', '-');
				int eq = trimmed.IndexOf('=');
				if (eq < 0)
				{
					options[trimmed] = "";
				}
				else
				{
					options[trimmed.Substring(0, eq)] = trimmed.Substring(eq + 1).Trim('"');
				}
			}
			return options;
		}

		static bool IsAdministrator()
		{
			return new WindowsPrincipal(WindowsIdentity.GetCurrent()).IsInRole(WindowsBuiltInRole.Administrator);
		}

		/// <summary>Runs this installer again as administrator and returns its exit code, or -1 if UAC was refused.</summary>
		static int Relaunch(string[] args)
		{
			try
			{
				var start = new ProcessStartInfo(Assembly.GetExecutingAssembly().Location);
				start.Arguments = string.Join(" ", args.Select(a => a.Contains(' ') ? "\"" + a + "\"" : a));
				start.Verb = "runas";
				start.UseShellExecute = true;
				using (Process elevated = Process.Start(start))
				{
					elevated.WaitForExit();
					return elevated.ExitCode;
				}
			}
			catch (Exception)
			{
				return -1;
			}
		}
	}

	sealed class InstallerForm : Form
	{
		readonly List<EngineInstall> Engines;
		readonly ListView EngineList = new ListView();
		readonly TextBox Output = new TextBox();
		readonly Button InstallButton = new Button();
		readonly Button UninstallButton = new Button();

		public InstallerForm(List<EngineInstall> engines)
		{
			Engines = engines;

			Text = "Spout UE " + Program.PluginVersion + " — installation";
			Font = new Font("Segoe UI", 9f);
			ClientSize = new Size(720, 460);
			MinimumSize = new Size(560, 380);
			StartPosition = FormStartPosition.CenterScreen;
			try
			{
				Icon = Icon.ExtractAssociatedIcon(Assembly.GetExecutingAssembly().Location);
			}
			catch (Exception)
			{
			}

			var title = new Label();
			title.Text = "Spout UE " + Program.PluginVersion;
			title.Font = new Font("Segoe UI Semibold", 14f);
			title.AutoSize = true;
			title.Location = new Point(16, 12);

			var intro = new Label();
			intro.Text = "Partage de textures GPU entre Unreal Engine et Resolume, OBS, TouchDesigner, MadMapper…\n"
				+ "Cochez les versions d'Unreal où installer le plugin (dossier Engine\\Plugins\\Marketplace).";
			intro.AutoSize = true;
			intro.Location = new Point(18, 46);

			EngineList.View = View.Details;
			EngineList.CheckBoxes = true;
			EngineList.FullRowSelect = true;
			EngineList.HeaderStyle = ColumnHeaderStyle.Nonclickable;
			EngineList.Location = new Point(16, 90);
			EngineList.Size = new Size(688, 150);
			EngineList.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
			EngineList.Columns.Add("Version", 90);
			EngineList.Columns.Add("Dossier", 360);
			EngineList.Columns.Add("État", 230);
			EngineList.ItemCheck += OnItemCheck;

			InstallButton.Text = "Installer";
			InstallButton.Size = new Size(120, 30);
			InstallButton.Location = new Point(16, 250);
			InstallButton.Click += delegate { Run(true); };

			UninstallButton.Text = "Désinstaller";
			UninstallButton.Size = new Size(120, 30);
			UninstallButton.Location = new Point(146, 250);
			UninstallButton.Click += delegate { Run(false); };

			var close = new Button();
			close.Text = "Fermer";
			close.Size = new Size(100, 30);
			close.Location = new Point(604, 250);
			close.Anchor = AnchorStyles.Top | AnchorStyles.Right;
			close.Click += delegate { Close(); };

			Output.Multiline = true;
			Output.ReadOnly = true;
			Output.ScrollBars = ScrollBars.Vertical;
			Output.Location = new Point(16, 292);
			Output.Size = new Size(688, 108);
			Output.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;

			var footer = new Label();
			footer.Text = "Ensuite, dans chaque projet : Edit > Plugins > Spout UE. Windows, DirectX 12 uniquement.";
			footer.AutoSize = true;
			footer.Location = new Point(16, 412);
			footer.Anchor = AnchorStyles.Bottom | AnchorStyles.Left;

			Controls.AddRange(new Control[] { title, intro, EngineList, InstallButton, UninstallButton, close, Output, footer });

			// The layout above is in 96 dpi pixels; WinForms scales the controls,
			// list view columns have to be scaled by hand.
			AutoScaleDimensions = new SizeF(96f, 96f);
			AutoScaleMode = AutoScaleMode.Dpi;
			float scale;
			using (Graphics graphics = CreateGraphics())
			{
				scale = graphics.DpiX / 96f;
			}
			foreach (ColumnHeader column in EngineList.Columns)
			{
				column.Width = (int)(column.Width * scale);
			}

			Program.Logged += line => Output.AppendText(line + Environment.NewLine);

			Populate();
			if (Engines.Count == 0)
			{
				Program.Write("Aucune version d'Unreal Engine n'a été trouvée sur cette machine.");
			}
			Program.Write("Builds inclus : UE " + string.Join(", UE ", Program.SupportedVersions()) + ".");
		}

		void Populate()
		{
			EngineList.Items.Clear();
			foreach (EngineInstall engine in Engines)
			{
				string installed = engine.InstalledPluginVersion();
				string state;
				if (!Program.HasPayload(engine))
				{
					state = "Pas de build pour " + engine.MajorMinor;
				}
				else if (installed == null)
				{
					state = "Non installé";
				}
				else
				{
					state = "Installé (" + installed + ")";
				}

				var item = new ListViewItem(new[] { "UE " + engine.Version, engine.Root, state });
				item.Tag = engine;
				item.Checked = Program.HasPayload(engine);
				if (!Program.HasPayload(engine))
				{
					item.ForeColor = SystemColors.GrayText;
				}
				EngineList.Items.Add(item);
			}
		}

		void OnItemCheck(object sender, ItemCheckEventArgs e)
		{
			// Versions without a build stay unticked: there is nothing to install.
			var engine = (EngineInstall)EngineList.Items[e.Index].Tag;
			if (e.NewValue == CheckState.Checked && !Program.HasPayload(engine))
			{
				e.NewValue = CheckState.Unchecked;
			}
		}

		void Run(bool install)
		{
			List<EngineInstall> selected = EngineList.Items.Cast<ListViewItem>()
				.Where(i => i.Checked)
				.Select(i => (EngineInstall)i.Tag)
				.ToList();
			if (selected.Count == 0)
			{
				Program.Write("Cochez au moins une version.");
				return;
			}

			UseWaitCursor = true;
			InstallButton.Enabled = UninstallButton.Enabled = false;
			Application.DoEvents();

			bool ok = install ? Program.Install(selected) : Program.Uninstall(selected);

			UseWaitCursor = false;
			InstallButton.Enabled = UninstallButton.Enabled = true;
			Populate();
			Program.Write(ok ? "Terminé." : "Terminé avec des erreurs (voir ci-dessus).");
		}
	}
}
