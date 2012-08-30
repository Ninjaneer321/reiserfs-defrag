#include "reiserfs.hpp"
#include <iostream>
#include <string.h>
#include <stdio.h>


Block::Block(FsJournal *journal_)
{
    memset(this->buf, 0, BLOCKSIZE);
    this->type = BLOCKTYPE_UNKNOWN;
    this->journal = journal_;
    this->dirty = false;
}

Block::~Block()
{
    if (this->dirty) this->write();
}

void
Block::attachJournal(FsJournal *journal_)
{
    this->journal = journal_;
}

void
Block::write()
{
    if (this->dirty) {
        this->journal->writeBlock(this);
        this->dirty = false;
    }
}

void
Block::rawDump() const
{
    for (int row = 0; row < BLOCKSIZE/16; row ++) {
        printf ("%08X  ", row*16 + this->block*BLOCKSIZE);

        int k;
        for (k = 0; k < 8; k ++) printf("%02X ", (unsigned char)buf[row*16+k]);
        printf ("|");
        for (k = 8; k < 16; k ++) printf(" %02X",(unsigned char)buf[row*16+k]);
        printf ("  |");
        for (k = 0; k < 16; k ++) {
            char c = buf[row*16+k];
            if (32 <= c && c < 127) printf("%c", c); else printf(".");
        }
        printf ("|\n");
    }
}

void
Block::formattedDump() const
{
    switch (this->type) {
        case BLOCKTYPE_UNKNOWN:
            std::cout << "unknown block" << std::endl;
            break;
        case BLOCKTYPE_INTERNAL:
            this->dumpInternalNodeBlock();
            break;
        case BLOCKTYPE_LEAF:
            this->dumpLeafNodeBlock();
            break;
        case BLOCKTYPE_UNFORMATTED:
            this->rawDump();
            break;
        default:
            std::cout << "block type error" << std::endl;
    }
}

void
Block::dumpInternalNodeBlock() const
{
    std::cout << "-- dumpInternalNodeBlock()  ------------" << std::endl;
    std::cout << "level = " << this->level() << std::endl;
    std::cout << "key count = " << this->keyCount() << std::endl;
    std::cout << "free space = " << this->freeSpace() << std::endl;
    for (int k = 0; k < this->keyCount(); k ++) {
        const struct tree_ptr ptr = this->getPtr(k);
        std::cout << "<" << ptr.block << ", " << ptr.size << "> ";
        this->getKey(k).dump_v1(std::cout, true);
    }
    const struct tree_ptr ptr = this->getPtr(this->keyCount());
    std::cout << "<" << ptr.block << ", " << ptr.size << ">" << std::endl;

    std::cout << "========================================" << std::endl;
}

void
Block::dumpLeafNodeBlock() const
{
    std::cout << "Block::dumpLeafNodeBlock() stub" << std::endl;
}

void
Block::do_move(std::map<uint32_t, uint32_t> &movemap)
{
    this->walk_tree(movemap);
}

void
Block::walk_tree(std::map<uint32_t, uint32_t> &movemap)
{
    if (this->type == BLOCKTYPE_INTERNAL) {
        for (int k = 0; k < this->ptrCount(); k ++) {
            Block *block_obj = this->journal->readBlock(this->getPtr(k).block);
            if (block_obj->level() > TREE_LEVEL_LEAF) {
                block_obj->setType(BLOCKTYPE_INTERNAL);
                block_obj->walk_tree(movemap);
            } else if (block_obj->level() == TREE_LEVEL_LEAF) {
                block_obj->setType(BLOCKTYPE_LEAF);
                std::cout << "Leaf Node, " << block_obj->block << std::endl;
                // process leaf contents
                for (int j = 0; j < block_obj->itemCount(); j ++) {
                    const struct item_header &ih = block_obj->itemHeader(j);
                    /* std::cout << key::type_name(ih.key.type(ih.version));
                    std::cout << ", count: " << ih.count;
                    std::cout << ", length: " << ih.length;
                    std::cout << ", offset: " << ih.offset;
                    std::cout << ", version: " << ih.version;
                    std::cout << std::endl; */

                    uint32_t key_version = ih.version;
                    uint32_t item_type = ih.key.type(ih.version);

                    // indirect items contain links to unformatted (data) blocks
                    if (KEY_TYPE_INDIRECT == item_type) {
                        for (int idx = 0; idx < ih.length/4; idx ++) {
                            uint32_t ref = block_obj->indirectItemRef(ih.offset, idx);
                            if (movemap.count(ref) == 0) continue;
                            // we have something to move
                            std::cout << "we have something to move";
                            // update indirect block
                            block_obj->setIndirectItemRef(ih.offset, idx, movemap[ref]);
                        }
                    }
                }
                std::cout << std::endl;
            } else {
                std::cerr << "error: unknown block in tree" << std::endl;
            }

            // do move

            delete block_obj;
        }
    }
}

void
Block::setType(int type_)
{
    this->type = type_;
}

int
Block::keyCount() const
{
    const struct blockheader &bh = reinterpret_cast<const struct blockheader &>(buf);
    return bh.bh_nr_items;
}

int
Block::ptrCount() const
{
    const struct blockheader &bh = reinterpret_cast<const struct blockheader &>(buf);
    return bh.bh_nr_items + 1;
}

int
Block::level() const
{
    const struct blockheader &bh = reinterpret_cast<const struct blockheader &>(buf);
    return bh.bh_level;
}

int
Block::freeSpace() const
{
    const struct blockheader &bh = reinterpret_cast<const struct blockheader &>(buf);
    return bh.bh_free_space;
}

int
Block::itemCount() const
{
    const struct blockheader &bh = reinterpret_cast<const struct blockheader &>(buf);
    return bh.bh_nr_items;
}

const struct Block::key &
Block::getKey(int index) const
{
    return reinterpret_cast<const struct key&>(buf[24 + 16*index]);
}

const struct Block::tree_ptr &
Block::getPtr(int index) const
{
    return reinterpret_cast<const struct tree_ptr&>(buf[24+16*keyCount()+8*index]);
}

const struct Block::item_header &
Block::itemHeader(int index) const
{
    return reinterpret_cast<const struct item_header&>(buf[24+24*index]);
}