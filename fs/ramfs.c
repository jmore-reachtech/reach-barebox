#include <common.h>
#include <driver.h>
#include <init.h>
#include <malloc.h>
#include <fs.h>
#include <command.h>
#include <errno.h>
#include <asm-generic/errno.h>
#include <linux/stat.h>
#include <xfuncs.h>

#define CHUNK_SIZE	512

struct ramfs_chunk {
	char *data;
	struct ramfs_chunk *next;
};

struct ramfs_inode {
	char *name;
	struct ramfs_inode *parent;
	struct ramfs_inode *next;
	struct ramfs_inode *child;
	ulong mode;

	struct handle_d *handle;

	ulong size;
	struct ramfs_chunk *data;
};

struct ramfs_priv {
	struct ramfs_inode root;
};

/* ---------------------------------------------------------------*/
struct ramfs_inode * lookup(struct ramfs_inode *node, const char *name)
{
//	printf("lookup: %s in %p\n",name, node);
	if(node->mode != S_IFDIR)
		return NULL;

	node = node->child;
	if (!node)
		return NULL;

	while (node) {
//		printf("lookup: %s\n", node->name);
		if (!strcmp(node->name, name)) {
//			printf("lookup: found: 0x%p\n",node);
			return node;
		}
		node = node->next;
	}
        return NULL;
}

struct ramfs_inode* rlookup(struct ramfs_priv *priv, const char *path)
{
	struct ramfs_inode *node = &priv->root;
        static char *buf;
        char *part;
//printf("rlookup %s in %p\n",path, node);
        buf = strdup(path);

        part = strtok(buf, "/");
        if (!part)
		goto out;

        do {
                node = lookup(node, part);
                if (!node)
			goto out;
                part = strtok(NULL, "/");
        } while(part);

out:
	free(buf);
        return node;
}

struct ramfs_inode* rlookup_parent(struct ramfs_priv *priv, const char *pathname, char **file)
{
	char *path;
	struct ramfs_inode *node;

	pathname++;
	path = strdup(pathname);

	if ((*file = strrchr(pathname, '/'))) {
		char *tmp;
		(*file)++;

		tmp = strrchr(path, '/');
		*tmp = 0;
		node = rlookup(priv, path);
		if (!node) {
			errno = -ENOENT;
			goto out;
		}
	} else {
		*file = (char *)pathname;
		node = &priv->root;
	}

out:
	free(path);
	return node;
}

static int chunks = 0;

struct ramfs_chunk *ramfs_get_chunk(void)
{
	struct ramfs_chunk *data = malloc(sizeof(struct ramfs_chunk));
//	printf("get chunk\n");
	data->data = malloc(CHUNK_SIZE);
	data->next = NULL;
	chunks++;
//	printf("chunks: %d\n", chunks);
	return data;
}

void ramfs_put_chunk(struct ramfs_chunk *data)
{
//	printf("put chunk\n");
	free(data->data);
	free(data);
	chunks--;
//	printf("chunks: %d\n", chunks);
}

static struct ramfs_inode* ramfs_get_inode(void)
{
	struct ramfs_inode *node = xzalloc(sizeof(struct ramfs_inode));
	return node;
}

static void ramfs_put_inode(struct ramfs_inode *node)
{
	struct ramfs_chunk *data = node->data;

	while (data) {
		struct ramfs_chunk *tmp = data->next;
		ramfs_put_chunk(data);
		data = tmp;
	}

	free(node->name);
	free(node);
}

struct ramfs_inode* node_insert(struct ramfs_inode *parent_node, const char *filename, ulong mode)
{
	struct ramfs_inode *node, *new_node = ramfs_get_inode();
	new_node->name = strdup(filename);
	new_node->mode = mode;

	node = parent_node->child;

	if (mode & S_IFDIR) {
		struct ramfs_inode *n = ramfs_get_inode();
		n->name = strdup(".");
		n->mode = S_IFDIR;
		n->child = n;
		n->parent = new_node;
		new_node->child = n;
		n = ramfs_get_inode();
		n->name = strdup("..");
		n->mode = S_IFDIR;
		n->parent = new_node;
		n->child = parent_node->child;
		new_node->child->next = n;
	} else {
		new_node->data = ramfs_get_chunk();
	}

	while (node->next)
		node = node->next;

	node->next = new_node;
	return new_node;
}

/* ---------------------------------------------------------------*/

int ramfs_create(struct device_d *dev, const char *pathname, mode_t mode)
{
	struct ramfs_priv *priv = dev->priv;
	struct ramfs_inode *node;
	char *file;

	node = rlookup_parent(priv, pathname, &file);
	node_insert(node, file, mode);
	return 0;
}

int ramfs_unlink(struct device_d *dev, const char *pathname)
{
	struct ramfs_priv *priv = dev->priv;
	struct ramfs_inode *node, *lastnode;
	char *file;

	node = rlookup_parent(priv, pathname, &file);

	lastnode = node->child->next;
	node = lastnode->next;

	while (node) {
		if (!strcmp(node->name, file)) {
			struct ramfs_inode *tmp;
			tmp = node;
			lastnode->next = node->next;
			ramfs_put_inode(tmp);
			return 0;
		}
		lastnode = node;
		node = node->next;
	};
	return -ENOENT;
}

int ramfs_mkdir(struct device_d *dev, const char *pathname)
{
	return ramfs_create(dev, pathname, S_IFDIR);
}

int ramfs_rmdir(struct device_d *dev, const char *pathname)
{
	struct ramfs_priv *priv = dev->priv;
	struct ramfs_inode *node, *lastnode;
	char *file;

	node = rlookup_parent(priv, pathname, &file);

	lastnode = node->child->next;
	node = lastnode->next;

	while (node) {
		if (!strcmp(node->name, file)) {
			struct ramfs_inode *tmp;
			tmp = node;
			lastnode->next = node->next;
			ramfs_put_inode(tmp->child->next);
			ramfs_put_inode(tmp->child);
			ramfs_put_inode(tmp);
			return 0;
		}
		lastnode = node;
		node = node->next;
	};
	return -ENOENT;
}

static int ramfs_open(struct device_d *dev, FILE *file, const char *filename)
{
	struct ramfs_priv *priv = dev->priv;
	struct ramfs_inode *node = rlookup(priv, filename);

	if (!node)
		return -ENOENT;

	file->size = node->size;
	file->inode = node;
	return 0;
}

static int ramfs_close(struct device_d *dev, FILE *f)
{
	return 0;
}

static int ramfs_read(struct device_d *_dev, FILE *f, void *buf, size_t insize)
{
	struct ramfs_inode *node = (struct ramfs_inode *)f->inode;
	int chunk;
	struct ramfs_chunk *data;
	int ofs;
	int now;
	int pos = f->pos;
	int size = insize;

	chunk = f->pos / CHUNK_SIZE;
//	printf("%s: reading from chunk %d\n", __FUNCTION__, chunk);

	/* Position ourself in stream */
	data = node->data;
	while (chunk) {
		data = data->next;
		chunk--;
	}
	ofs = f->pos % CHUNK_SIZE;

	/* Read till end of current chunk */
	if (ofs) {
		now = min(size, CHUNK_SIZE - ofs);
//		printf("Reading till end of node. size: %d\n", size);
		memcpy(buf, data->data + ofs, now);
		size -= now;
		pos += now;
		buf += now;
		if (pos > node->size)
			node->size = now;
		data = data->next;
	}

	/* Do full chunks */
	while (size >= CHUNK_SIZE) {
//		printf("do full chunk. size: %d\n", size);
		memcpy(buf, data->data, CHUNK_SIZE);
		data = data->next;
		size -= CHUNK_SIZE;
		pos += CHUNK_SIZE;
		buf += CHUNK_SIZE;
	}

	/* And the rest */
	if (size) {
//		printf("do rest. size: %d\n", size);
		memcpy(buf, data->data, size);
	}

	return insize;
}

static int ramfs_write(struct device_d *_dev, FILE *f, const void *buf, size_t insize)
{
	struct ramfs_inode *node = (struct ramfs_inode *)f->inode;
	int chunk;
	struct ramfs_chunk *data;
	int ofs;
	int now;
	int pos = f->pos;
	int size = insize;

	chunk = f->pos / CHUNK_SIZE;
//	printf("%s: writing to chunk %d\n", __FUNCTION__, chunk);

	/* Position ourself in stream */
	data = node->data;
	while (chunk) {
		data = data->next;
		chunk--;
	}
	ofs = f->pos % CHUNK_SIZE;

	/* Write till end of current chunk */
	if (ofs) {
		now = min(size, CHUNK_SIZE - ofs);
//		printf("writing till end of node. size: %d\n", size);
		memcpy(data->data + ofs, buf, now);
		size -= now;
		pos += now;
		buf += now;
		if (pos > node->size)
			node->size = now;
		data = data->next;
	}

	/* Do full chunks */
	while (size >= CHUNK_SIZE) {
//		printf("do full chunk. size: %d\n", size);
		memcpy(data->data, buf, CHUNK_SIZE);
		data = data->next;
		size -= CHUNK_SIZE;
		pos += CHUNK_SIZE;
		buf += CHUNK_SIZE;
	}

	/* And the rest */
	if (size) {
//		printf("do rest. size: %d\n", size);
		memcpy(data->data, buf, size);
	}

	return insize;
}

int ramfs_truncate(struct device_d *dev, FILE *f, ulong size)
{
	struct ramfs_inode *node = (struct ramfs_inode *)f->inode;
	int oldchunks, newchunks;
	struct ramfs_chunk *data = node->data;

	newchunks = (size + CHUNK_SIZE - 1) / CHUNK_SIZE;
	oldchunks = (node->size + CHUNK_SIZE - 1) / CHUNK_SIZE;

	if (newchunks < oldchunks) {
		while (newchunks--)
			data = data->next;
		while (data->next) {
			struct ramfs_chunk *tmp;
			tmp = data->next;
			ramfs_put_chunk(data);
			data = tmp;
		}
	}

	if (newchunks > oldchunks) {
		while (data->next) {
			newchunks--;
			data = data->next;
		}

		while (newchunks--) {
			data->next = ramfs_get_chunk();
			data = data->next;
		}
	}
	node->size = size;
	return 0;
}

struct dir* ramfs_opendir(struct device_d *dev, const char *pathname)
{
	struct dir *dir;
	struct ramfs_priv *priv = dev->priv;
	struct ramfs_inode *node;
// printf("opendir: %s\n", pathname);

	node = rlookup(priv, pathname);
//printf("opendir name: %s\n", node->name);
	if (!node)
		return NULL;

	if (node->mode != S_IFDIR)
		return NULL;

	dir = malloc(sizeof(struct dir));
	if (!dir)
		return NULL;

	dir->priv = node->child;

	return dir;
}

struct dirent* ramfs_readdir(struct device_d *dev, struct dir *dir)
{
	struct ramfs_inode *node = dir->priv;

	if (node) {
		strcpy(dir->d.name, node->name);
		dir->priv = node->next;
		return &dir->d;
	}
	return NULL;
}

int ramfs_closedir(struct device_d *dev, struct dir *dir)
{
	free(dir);
	return 0;
}

int ramfs_stat(struct device_d *dev, const char *filename, struct stat *s)
{
	struct ramfs_priv *priv = dev->priv;
	struct ramfs_inode *node = rlookup(priv, filename);

//	printf("ramfs_stat: %s\n", filename);
	if (!node)
		return -ENOENT;
//	printf("stat: %s\n", node->name);
	s->st_size = node->size;
	s->st_mode = node->mode;

	return 0;
}

int ramfs_probe(struct device_d *dev)
{
	struct ramfs_inode *n;
	struct ramfs_priv *priv = xzalloc(sizeof(struct ramfs_priv));

//	printf("ramfs_probe\n");

	dev->priv = priv;

	priv->root.name = "/";
	priv->root.mode = S_IFDIR;
	priv->root.parent = &priv->root;
	n = ramfs_get_inode();
	n->name = strdup(".");
	n->mode = S_IFDIR;
	n->parent = &priv->root;
	n->child = n;
	priv->root.child = n;
	n = ramfs_get_inode();
	n->name = strdup("..");
	n->mode = S_IFDIR;
	n->parent = &priv->root;
	n->child = priv->root.child;
	priv->root.child->next = n;
//	printf("priv->root: 0x%p\n", priv->root);

	return 0;
}

static struct fs_driver_d ramfs_driver = {
	.type      = FS_TYPE_RAMFS,
	.create    = ramfs_create,
	.unlink    = ramfs_unlink,
	.open      = ramfs_open,
	.close     = ramfs_close,
	.truncate  = ramfs_truncate,
	.read      = ramfs_read,
	.write     = ramfs_write,
	.mkdir     = ramfs_mkdir,
	.rmdir     = ramfs_rmdir,
	.opendir   = ramfs_opendir,
	.readdir   = ramfs_readdir,
	.closedir  = ramfs_closedir,
	.stat      = ramfs_stat,
	.flags     = FS_DRIVER_NO_DEV,
	.drv = {
		.type   = DEVICE_TYPE_FS,
		.probe  = ramfs_probe,
		.name = "ramfs",
		.type_data = &ramfs_driver,
	}
};

int ramfs_init(void)
{
	return register_driver(&ramfs_driver.drv);
}

device_initcall(ramfs_init);
