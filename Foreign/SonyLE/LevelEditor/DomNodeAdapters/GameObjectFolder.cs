//Copyright � 2014 Sony Computer Entertainment America LLC. See License.txt.

using System.Collections.Generic;

using Sce.Atf;
using Sce.Atf.Adaptation;
using Sce.Atf.Applications;
using Sce.Atf.Dom;

using LevelEditorCore;

namespace LevelEditor.DomNodeAdapters
{
    /// <summary>
    /// DomNodeAdapter for game object folders</summary>
    public class GameObjectFolder : DomNodeAdapter, IGameObjectFolder, IHierarchical, INameable, IVisible, IListable, ILockable
    {
        #region INameable Members

        /// <summary>
        /// Gets and sets the name</summary>
        public string Name
        {
            get { return GetAttribute<string>(Schema.gameObjectFolderType.nameAttribute); }
            set { SetAttribute(Schema.gameObjectFolderType.nameAttribute, value); }
        }

        #endregion

        #region IGameObjectFolder Members
        /// <summary>
        /// Gets the list of game objects</summary>
        public IList<IGameObject> GameObjects
        {
            get { return GetChildList<IGameObject>(Schema.gameObjectFolderType.gameObjectChild); }
        }

        /// <summary>
        /// Gets the list of child game object folders</summary>
        public IList<IGameObjectFolder> GameObjectFolders
        {
            get { return GetChildList<IGameObjectFolder>(Schema.gameObjectFolderType.folderChild); }
        }

        public ITransformableGroup CreateGroup()
        {
            var gobGroup = new DomNode(Schema.transformObjectGroupType.Type).As<ITransformableGroup>();
            // gobGroup.Name = "Group".Localize("this is the name of a folder in the project lister");
            return gobGroup;
        }
        #endregion

        #region IVisible Members

        /// <summary>
        /// Get or sets the object's visibility state</summary>
        public bool Visible
        {
            get { return GetAttribute<bool>(Schema.gameObjectFolderType.visibleAttribute) && this.AncestorIsVisible(); }
            set { SetAttribute(Schema.gameObjectFolderType.visibleAttribute, value); }
        }

        #endregion

        #region ILockable Members

        /// <summary>
        /// Gets or sets a value indicating if the DomNode is locked</summary>
        public bool IsLocked
        {
            get { return GetAttribute<bool>(Schema.gameObjectFolderType.lockedAttribute) || this.AncestorIsLocked(); }
            set { SetAttribute(Schema.gameObjectFolderType.lockedAttribute, value); }
        }       
        #endregion

        #region IListable Members

        /// <summary>
        /// Gets display info (label, icon, ...) for the ProjectLister and other controls</summary>
        /// <param name="info">Item info: passed in and modified by this method</param>
        public void GetInfo(ItemInfo info)
        {
            info.ImageIndex = Util.GetTypeImageIndex(DomNode.Type, info.GetImageList());
            info.Label = Name;
            info.IsLeaf = (GameObjects.Count == 0) && (GameObjectFolders.Count == 0);

            if (IsLocked)
                info.StateImageIndex = info.GetImageList().Images.IndexOfKey(Sce.Atf.Resources.LockImage);
        }

        #endregion

        #region IHierarchical Members

        /// <summary>
        /// Returns true iff the specified child can be inserted</summary>
        /// <param name="child">Child to be inserted</param>
        /// <returns>True iff the specified child can be inserted</returns>
        public bool CanAddChild(object child)
        {
            if (Adapters.Is<IGameObjectFolder>(child))
            {
                DomNode childNode = Adapters.As<DomNode>(child);
                if (childNode == null || childNode == DomNode || DomNode.IsDescendantOf(childNode))
                    return false;
                return true;
            }
            return Adapters.Is<IGameObject>(child);
        }

        /// <summary>
        /// Inserts the specified object as a child of this object</summary>
        /// <param name="child">Child to be inserted</param>
        public bool AddChild(object child)
        {
            bool added = false;
            // Add sub GameObjectFolders to the folder
            IGameObjectFolder folder = Adapters.As<IGameObjectFolder>(child);
            if (folder != null)
            {
                GameObjectFolders.Add(folder);
                added = true;
            }
            else
            {
                // Add GameObjects to the folder
                IGameObject gameObject = Adapters.As<IGameObject>(child);
                if (gameObject != null)
                {
                    GameObjects.Add(gameObject);
                    added = true;
                }
            }
            return added;
        }
        #endregion

        public static IGameObjectFolder Create()
        {
            GameObjectFolder gobFolder = new DomNode(Schema.gameObjectFolderType.Type).Cast<GameObjectFolder>();
            gobFolder.Name = "GameObjectFolder".Localize("this is the name of a folder in the project lister");
            return gobFolder;
        }
    }
}
