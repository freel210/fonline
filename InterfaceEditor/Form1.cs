﻿using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.Text;
using System.Windows.Forms;
using System.IO;
using System.Runtime.Serialization;
using Newtonsoft.Json;
using Newtonsoft.Json.Converters;
using System.Net;
using System.Reflection;

namespace InterfaceEditor
{
	public partial class MainForm : Form
	{
		static public MainForm Instance;

		public string TreeFileName;
		public string SchemeName;

		public MainForm()
		{
			Instance = this;
			InitializeComponent();
			Directory.SetCurrentDirectory(Path.GetDirectoryName(Application.ExecutablePath));

			// Optimize panel drawing
			typeof(Panel).InvokeMember("DoubleBuffered",
				BindingFlags.SetProperty | BindingFlags.Instance | BindingFlags.NonPublic,
				null, Design, new object[] { true });

			// Setup data path
			IniFile config = new IniFile(".\\InterfaceEditor.cfg");
			string dataPath = config.IniReadValue("Options", "ClientPath", "..\\..\\Client\\");
			dataPath = dataPath.Replace('/', '\\');
			if (!dataPath.EndsWith("\\"))
				dataPath += "\\";
			dataPath += "data\\";
			dataPath = Path.GetFullPath(dataPath);
			Utilites.DataPath = dataPath;

			// Setup scripts path
			Utilites.ScriptsPath = config.IniReadValue("Options", "ServerPath", "..\\..\\Server\\") + "scripts\\";

			// Load default scheme
			LoadScheme("gui\\default.foguischeme");

			// Hierarchy drag and drop
			Hierarchy.ItemDrag += delegate(object sender, ItemDragEventArgs e)
			{
				DoDragDrop(e.Item, DragDropEffects.Move);
			};
			Hierarchy.DragEnter += delegate(object sender, DragEventArgs e)
			{
				e.Effect = DragDropEffects.Move;
			};
			Hierarchy.DragDrop += delegate(object sender, DragEventArgs e)
			{
				if (e.Data.GetDataPresent("System.Windows.Forms.TreeNode", false))
				{
					TreeView tree = (TreeView)sender;
					Point pt = tree.PointToClient(new Point(e.X, e.Y));
					TreeNode destNode = tree.GetNodeAt(pt);
					TreeNode srcNode = (TreeNode)e.Data.GetData("System.Windows.Forms.TreeNode");
					if (srcNode.Parent != null && destNode != null && srcNode != destNode && srcNode.Nodes.Find(destNode.Name, true).Length == 0)
						((GUIObject)srcNode.Tag).AssignParent((GUIObject)destNode.Tag);
				}
			};

			// Hierarchy node selection
			Hierarchy.NodeMouseClick += delegate(object sender, TreeNodeMouseClickEventArgs e)
			{
				Hierarchy.SelectedNode = e.Node;
				Design.Invalidate();
			};
			Hierarchy.AfterSelect += delegate(object sender, TreeViewEventArgs e)
			{
				Properties.SelectedObject = e.Node.Tag;
				Design.Invalidate();
			};

			// Hierarchy menu strip
			ToolStripMenuItem addMenuStrip = new ToolStripMenuItem("Add");
			addMenuStrip.DropDownItems.Add("Image").Tag = (Action<GUIObject>)delegate(GUIObject obj) { new GUIImage(obj).RefreshRepresentation(false); };
			addMenuStrip.DropDownItems.Add("Panel").Tag = (Action<GUIObject>)delegate(GUIObject obj) { new GUIPanel(obj).RefreshRepresentation(false); };
			addMenuStrip.DropDownItems.Add("Button").Tag = (Action<GUIObject>)delegate(GUIObject obj) { new GUIButton(obj).RefreshRepresentation(false); };
			addMenuStrip.DropDownItems.Add("Text").Tag = (Action<GUIObject>)delegate(GUIObject obj) { new GUIText(obj).RefreshRepresentation(false); };
			addMenuStrip.DropDownItems.Add("Text Input").Tag = (Action<GUIObject>)delegate(GUIObject obj) { new GUITextInput(obj).RefreshRepresentation(false); };
			addMenuStrip.DropDownItems.Add("Message Box").Tag = (Action<GUIObject>)delegate(GUIObject obj) { new GUIMessageBox(obj).RefreshRepresentation(false); };
			addMenuStrip.DropDownItems.Add("Console").Tag = (Action<GUIObject>)delegate(GUIObject obj) { new GUIConsole(obj).RefreshRepresentation(false); };

			ToolStripMenuItem moveUpMenuStrip = new ToolStripMenuItem("Move node up");
			moveUpMenuStrip.Tag = (Action<GUIObject>)delegate(GUIObject obj) { if (obj != null) obj.MoveUp(); };

			ToolStripMenuItem moveDownMenuStrip = new ToolStripMenuItem("Move node down");
			moveDownMenuStrip.Tag = (Action<GUIObject>)delegate(GUIObject obj) { if (obj != null) obj.MoveDown(); };

			ToolStripMenuItem deleteMenuStrip = new ToolStripMenuItem("Delete node");
			deleteMenuStrip.Tag = (Action<GUIObject>)delegate(GUIObject obj) { if (obj != null) obj.Delete(); };

			ToolStripMenuItem newTreeMenuStrip = new ToolStripMenuItem("New GUI");
			newTreeMenuStrip.Tag = (Action<GUIObject>)delegate(GUIObject obj) { NewTree(); };

			ToolStripMenuItem loadTreeMenuStrip = new ToolStripMenuItem("Load GUI");
			loadTreeMenuStrip.Tag = (Action<GUIObject>)delegate(GUIObject obj) { LoadAndShowTree(); };

			ToolStripMenuItem saveTreeMenuStrip = new ToolStripMenuItem("Save GUI");
			saveTreeMenuStrip.Tag = (Action<GUIObject>)delegate(GUIObject obj) { SaveTree(obj, false); GenerateScheme(); };

			ToolStripMenuItem saveAsTreeMenuStrip = new ToolStripMenuItem("Save GUI As");
			saveAsTreeMenuStrip.Tag = (Action<GUIObject>)delegate(GUIObject obj) { SaveTree(obj, true); GenerateScheme(); };

			ContextMenuStrip hierarchyMenuStrip = new ContextMenuStrip();
			hierarchyMenuStrip.Items.Add(addMenuStrip);
			hierarchyMenuStrip.Items.Add(moveUpMenuStrip);
			hierarchyMenuStrip.Items.Add(moveDownMenuStrip);
			hierarchyMenuStrip.Items.Add(deleteMenuStrip);
			hierarchyMenuStrip.Items.Add(newTreeMenuStrip);
			hierarchyMenuStrip.Items.Add(loadTreeMenuStrip);
			hierarchyMenuStrip.Items.Add(saveTreeMenuStrip);
			hierarchyMenuStrip.Items.Add(saveAsTreeMenuStrip);

			ToolStripItemClickedEventHandler clickHandler = delegate(object sender, ToolStripItemClickedEventArgs e)
			{
				if (e.ClickedItem.Tag != null)
				{
					hierarchyMenuStrip.Close();
					GUIObject curObj = (Hierarchy.SelectedNode != null ? (GUIObject)Hierarchy.SelectedNode.Tag : null);
					if (curObj == null && addMenuStrip.DropDownItems.Contains(e.ClickedItem))
						MessageBox.Show("Create or load GUI first.");
					else
						((Action<GUIObject>)e.ClickedItem.Tag)(curObj);
				}
			};

			addMenuStrip.DropDownItemClicked += clickHandler;
			hierarchyMenuStrip.ItemClicked += clickHandler;
			Hierarchy.ContextMenuStrip = hierarchyMenuStrip;

			// Design drag handler
			bool designDragging = false;
			Point designDragMouseStart = Point.Empty;
			Point designDragObjectStart = Point.Empty;
			Design.MouseDown += delegate(object sender, MouseEventArgs e)
			{
				if (Hierarchy.SelectedNode != null)
				{
					GUIObject obj = (GUIObject)Hierarchy.SelectedNode.Tag;
					if (obj.IsHit(e.X, e.Y))
					{
						designDragging = true;
						designDragMouseStart = new Point(e.X, e.Y);
						designDragObjectStart = obj.Position;
						Design.Capture = true;
					}
				}
			};
			Design.MouseUp += delegate(object sender, MouseEventArgs e)
			{
				designDragging = false;
				Design.Capture = false;
			};
			Design.MouseMove += delegate(object sender, MouseEventArgs e)
			{
				if (designDragging && Hierarchy.SelectedNode != null)
				{
					GUIObject obj = (GUIObject)Hierarchy.SelectedNode.Tag;
					Point position = obj.Position;
					position.X = designDragObjectStart.X + e.X - designDragMouseStart.X;
					position.Y = designDragObjectStart.Y + e.Y - designDragMouseStart.Y;
					obj.Position = position;
					Properties.Refresh();
					Design.Refresh();
				}
			};
		}

		private void SaveTree(GUIObject obj, bool saveAs)
		{
			if (obj == null)
				return;
			if (saveAs && !GetTreeFileName(true))
				return;

			GUIScreen root = obj.GetRoot();

			JsonSerializerSettings settings = new JsonSerializerSettings();
			settings.TypeNameHandling = TypeNameHandling.Objects;
			settings.Binder = new TypeBinder();
			settings.Converters.Add(new StringEnumConverter());
			string data = JsonConvert.SerializeObject(root, Formatting.Indented, settings);
			File.WriteAllText(TreeFileName, data, Encoding.UTF8);
		}

		private void LoadAndShowTree()
		{
			if (!GetTreeFileName(false))
				return;

			GUIScreen root = LoadTree(TreeFileName);

			while (Hierarchy.Nodes.Count > 0)
				((GUIObject)Hierarchy.Nodes[0].Tag).Delete();

			root.RefreshRepresentation(true);
			Hierarchy.Nodes[0].ExpandAll();
			Hierarchy.SelectedNode = Hierarchy.Nodes[0];
		}

		private GUIScreen LoadTree(string fileName)
		{
			string data = File.ReadAllText(fileName, Encoding.UTF8);
			JsonSerializerSettings settings = new JsonSerializerSettings();
			settings.TypeNameHandling = TypeNameHandling.Objects;
			settings.Binder = new TypeBinder();
			settings.Converters.Add(new StringEnumConverter());
			GUIObject obj = JsonConvert.DeserializeObject<GUIObject>(data, settings);
			return obj.GetRoot();
		}

		private GUIScreen NewTree()
		{
			if (!GetTreeFileName(true))
				return null;

			if (Hierarchy.Nodes.Count > 0)
				((GUIObject)Hierarchy.Nodes[0].Tag).Delete();

			GUIScreen root = new GUIScreen(null);
			root.RefreshRepresentation(true);

			SaveTree(root, false);

			return root;
		}

		private bool GetTreeFileName(bool newFile)
		{
			if (!newFile)
			{
				OpenFileDialog fileDialog = new OpenFileDialog();
				fileDialog.Filter = "FOnline GUI files (*.fogui) | *.fogui";
				fileDialog.InitialDirectory = Path.GetDirectoryName(Application.ExecutablePath) + "\\gui";
				if (fileDialog.ShowDialog() != DialogResult.OK)
					return false;
				TreeFileName = fileDialog.FileName;
			}
			else
			{
				SaveFileDialog fileDialog = new SaveFileDialog();
				fileDialog.Filter = "FOnline GUI files (*.fogui) | *.fogui";
				fileDialog.InitialDirectory = Path.GetDirectoryName(Application.ExecutablePath) + "\\gui";
				if (fileDialog.ShowDialog() != DialogResult.OK)
					return false;
				TreeFileName = fileDialog.FileName;
			}
			return true;
		}

		private void buttonLoadScheme_Click(object sender, EventArgs e)
		{
			OpenFileDialog fileDialog = new OpenFileDialog();
			fileDialog.Filter = "FOnline GUI files (*.foguischeme) | *.foguischeme";
			fileDialog.InitialDirectory = Path.GetDirectoryName(Application.ExecutablePath) + "\\gui";
			if (fileDialog.ShowDialog() != DialogResult.OK)
				return;

			LoadScheme(fileDialog.FileName);
		}

		private void LoadScheme(string fileName)
		{
			if (!File.Exists(fileName))
				return;

			string text = File.ReadAllText(fileName);
			string[] textLines = text.Split(new string[] { "\r\n", "\n" }, StringSplitOptions.RemoveEmptyEntries);
			dataGridViewScheme.Rows.Clear();
			foreach (string textLine in textLines)
			{
				string[] lineData = textLine.Split(new char[] { ' ' }, StringSplitOptions.RemoveEmptyEntries);
				dataGridViewScheme.Rows.Add(lineData[0], lineData[1]);
			}

			SchemeName = Path.GetFileNameWithoutExtension(fileName);
		}

		private void buttonSaveScheme_Click(object sender, EventArgs e)
		{
			SaveFileDialog fileDialog = new SaveFileDialog();
			fileDialog.Filter = "FOnline GUI files (*.foguischeme) | *.foguischeme";
			fileDialog.InitialDirectory = Path.GetDirectoryName(Application.ExecutablePath) + "\\gui";
			if (fileDialog.ShowDialog() != DialogResult.OK)
				return;

			string text = "";
			for (int i = 0; i < dataGridViewScheme.RowCount; i++)
			{
				string screen = (string)dataGridViewScheme.Rows[i].Cells[0].Value;
				string guiFile = (string)dataGridViewScheme.Rows[i].Cells[1].Value;
				if (!string.IsNullOrEmpty(screen))
					text += screen + "    " + (!string.IsNullOrEmpty(guiFile) ? guiFile : "none") + Environment.NewLine;
			}
			File.WriteAllText(fileDialog.FileName, text);
		}

		private void buttonGenerateScheme_Click(object sender, EventArgs e)
		{
			GenerateScheme();
		}

		private void GenerateScheme()
		{
			StringBuilder initScript = new StringBuilder(100000);
			StringBuilder contentScript = new StringBuilder(100000);

			initScript.AppendLine("// GUI scheme name: " + SchemeName);
			initScript.AppendLine();
			initScript.AppendLine("#include \"_macros.fos\"");
			initScript.AppendLine("#include \"_client_defines.fos\"");
			initScript.AppendLine("#include \"_colors.fos\"");
			initScript.AppendLine("#include \"_msgstr.fos\"");
			initScript.AppendLine("#include \"gui_h.fos\"");
			initScript.AppendLine("#include \"gui_screens_stuff.fos\"");
			initScript.AppendLine();
			initScript.AppendLine("void InitializeScreens()");
			initScript.AppendLine("{");

			for (int i = 0; i < dataGridViewScheme.RowCount; i++)
			{
				string screen = (string)dataGridViewScheme.Rows[i].Cells[0].Value;
				string guiFile = (string)dataGridViewScheme.Rows[i].Cells[1].Value;
				if (!string.IsNullOrEmpty(screen) && !string.IsNullOrEmpty(guiFile) && !guiFile.StartsWith("*"))
				{
					string guiFileFullPath = Path.GetDirectoryName(Application.ExecutablePath) + "\\gui\\" + guiFile;
					GUIScreen root = LoadTree(guiFileFullPath);

					string scriptPrefix = screen.Substring(screen.IndexOf("SCREEN_") + 7).ToLower();
					scriptPrefix = scriptPrefix.Substring(0, 1).ToUpper() + scriptPrefix.Substring(1);

					contentScript.AppendLine();
					contentScript.AppendLine("namespace " + scriptPrefix);
					contentScript.AppendLine("{");

					string scriptContent = new ScriptWriter().Write(root, "    ");
					contentScript.Append(scriptContent);

					contentScript.AppendLine("} // namespace " + scriptPrefix);

					initScript.AppendLine("    " + scriptPrefix + "::Init( " + screen + " );");
				}
			}

			initScript.AppendLine("}");

			File.WriteAllText(Utilites.ScriptsPath + "gui_screens.fos", initScript.ToString() + contentScript.ToString(), Encoding.UTF8);

			Log.Write("Script \"gui_screens.fos\" generation complete.");
		}

		private void Design_Paint(object sender, PaintEventArgs e)
		{
			if (Hierarchy.Nodes.Count > 0)
				((GUIObject)Hierarchy.Nodes[0].Tag).Draw(e.Graphics);
		}
	}

	class TypeBinder : SerializationBinder
	{
		public override Type BindToType(string assemblyName, string typeName)
		{
			return Type.GetType(String.Format("{0}, {1}", "InterfaceEditor.GUI" + typeName, "InterfaceEditor"));
		}

		public override void BindToName(Type serializedType, out string assemblyName, out string typeName)
		{
			string name = serializedType.ToString();
			name = name.Substring(name.IndexOf(".GUI") + 4);
			assemblyName = null;
			typeName = name;
		}
	}
}