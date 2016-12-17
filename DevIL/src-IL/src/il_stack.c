//-----------------------------------------------------------------------------
//
// ImageLib Sources
// Copyright (C) 2000-2008 by Denton Woods
//
// Filename: src-IL/src/il_stack.c
//
// Description: The main image stack
//
//-----------------------------------------------------------------------------

// Credit goes to John Villar (johnny@reliaschev.com) for making the suggestion
//	of not letting the user use ILimage structs but instead binding images
//	like OpenGL.

#include "il_internal.h"
#include "il_stack.h"

// TODO: this should be in IL/il.h
ILboolean ILAPIENTRY ilActive(ILuint frame, ILuint mipmap, ILuint layer, ILuint face);

typedef struct SlotID {
    ILuint frame,mipmap,layer,face;
} SlotID;


typedef struct SlotEntry {
    SlotID id;
    ILimage *img;
} SlotEntry;

typedef struct Slot {
    SlotID extents;
    ILuint capacity;
    ILuint length;
    SlotEntry* images;
} Slot;


typedef struct Stack {
    ILuint length;
    ILuint capacity;
    Slot** slots;
} Stack;


// TODO: iCurImage should be here too.
ILuint CurName = 0;
SlotID CurActive = {0};

static ILboolean IsInit = IL_FALSE;
static const SlotID slotid_ZERO  = {0};
static Stack imageStack = {0};

// Internal stuff

//static ILboolean	OnExit = IL_FALSE;
//static ILboolean	ParentImage = IL_TRUE;


static int slotid_cmp(const SlotID a, const SlotID b);
static Slot* slot_create();
static int slot_add(Slot* slot, const SlotID id);
static void slot_delete(Slot* slot);
static int slot_findimage(Slot* slot, const SlotID id);
static ILboolean stack_init(Stack* s);
static void stack_cleanup(Stack* s);
static ILboolean stack_allocslot(Stack* s, ILuint *found);


static int slotid_cmp(const SlotID a, const SlotID b)
{
    if (a.frame>b.frame) {
        return 1;
    } else if (a.frame<b.frame) {
        return -1;
    }

    if (a.mipmap>b.mipmap) {
        return 1;
    } else if (a.mipmap<b.mipmap) {
        return -1;
    }

    if (a.layer>b.layer) {
        return 1;
    } else if (a.layer<b.layer) {
        return -1;
    }

    if (a.face>b.face) {
        return 1;
    } else if (a.face<b.face) {
        return -1;
    }

    // if we got this far, they're equal!
    return 0;
}


static Slot* slot_create()
{
    const int initialcap = 8;
    Slot* slot = ialloc(sizeof(Slot));

    if (slot==NULL) {
        return NULL;
    }

    slot->extents = slotid_ZERO;
    slot->length = 0;
    slot->images = ialloc(initialcap*sizeof(SlotEntry));
    slot->capacity = initialcap;
    if (slot->images==NULL) {
        ifree(slot);
        return NULL;
    }

    // add default image (ie frame0, mip0, layer0 face0)
    if (slot_add(slot, slotid_ZERO) <0) {
        slot_delete(slot);
        return NULL;
    }

    return slot;
}


// delete a slot and all it's images
static void slot_delete(Slot* slot)
{
    int i;
    for(i=0; i<slot->length; ++i) {
        ilCloseImage( slot->images[i].img );
    }
    ifree(slot->images);
    ifree(slot);
}


// adds a new entry into the slot (assumes that id isn't present!)
static int slot_add(Slot* slot, const SlotID id)
{
    ILimage* img;
    int idx;

    if (slot->length >= slot->capacity) {
        // grow the slot...
        int newcap = slot->capacity*2;
        SlotEntry* newmem = ialloc(newcap * sizeof(SlotEntry));
        if (newmem==NULL ) {
            return -1;
        }
        memcpy( newmem, slot->images, slot->length*sizeof(SlotEntry));
        ifree(slot->images);
        slot->images = newmem;
        slot->capacity = newcap;
    }

    img = ilNewImage(1,1,1,1,1);
    if (img==NULL) {
        return -1;
    }

    // TODO: insert in order of id, so we can binary search
    // but for now... just append
    idx = slot->length;
    ++slot->length;
    slot->images[idx].id = id;
    slot->images[idx].img = img;

    // update the maximum frame/mip/layer/face values
    if (id.frame > slot->extents.frame ) {
        slot->extents.frame = id.frame;
    }
    if (id.mipmap > slot->extents.mipmap ) {
        slot->extents.mipmap = id.mipmap;
    }
    if (id.layer > slot->extents.layer ) {
        slot->extents.layer = id.layer;
    }
    if (id.face > slot->extents.face ) {
        slot->extents.face = id.face;
    }

    return idx;
}




static int slot_findimage(Slot* slot, const SlotID id)
{
    // TODO: just a linear search. fine for small sets, but the idea is that
    // we'll keep the list sorted, so we can do a quick binary search instead.
    int i;
    for(i=0; i<slot->length; ++i) {
        if(slotid_cmp(id, slot->images[i].id) == 0 ) {
            return i;
        }
    }
    return -1;
}




static ILboolean stack_init(Stack* s)
{
    s->length = 0;
    s->capacity = 16;
    s->slots = ialloc(s->capacity * sizeof(Slot));
    if (s->slots == NULL) {
        return IL_FALSE;
    }


    // create default image
    s->slots[0] = slot_create();
    if (s->slots[0]==NULL) {
        return IL_FALSE;
    }
    // TODO: use ilDefaultImage to generate default image date

    // create temp image
    s->slots[1] = slot_create();
    if (s->slots[1]==NULL) {
        return IL_FALSE;
    }
}


static void stack_cleanup(Stack* s)
{
    if(s->slots == NULL) {
        return;
    }

    ILuint i;
    for (i=0; i<s->length; ++i) {
        if (s->slots[i]==NULL) {
            continue;
        }
        slot_delete(s->slots[i]);
    }
    ifree(s->slots);
    // just to be tidy...
    s->length = 0;
    s->capacity = 0;
    s->slots=NULL;
}



// return -1 if out of memory
static ILboolean stack_allocslot(Stack* s, ILuint *found)
{
    ILuint idx;
    if(s->length < s->capacity) {
        *found = s->length;
        ++s->length;
        return IL_TRUE;
    }

    // We're at capacity. Is there one we can reuse?
    for (idx=0; idx<s->length; ++idx) {
        if( s->slots[idx] == NULL ) {
            *found = idx;
            return IL_TRUE;
        }
    }

    // nope. grow.
    {
        int newcap = s->capacity*2;
        Slot** newmem;
        newmem = ialloc( newcap * sizeof(Slot) );
        if (newmem==NULL) {
            return IL_FALSE;
        }
        memcpy(newmem, s->slots, s->length * sizeof(Slot) );
        ifree(s->slots);
        s->slots = newmem;
        s->capacity = newcap;
        // clear the newly-added space
        for (idx=s->length; idx<s->capacity; ++idx) {
            s->slots[idx] = NULL;
        }

        // now we can allocate one
        *found = s->length;
        ++s->length;
        return IL_TRUE;
    }
}




//! Creates Num images and puts their index in Images - similar to glGenTextures().
void ILAPIENTRY ilGenImages(ILsizei Num, ILuint *Images)
{
	if (Num < 1 || Images == NULL) {
		ilSetError(IL_INVALID_VALUE);
		return;
	}

	ILsizei	i;
    for (i=0; i<Num; ++i) {
        ILuint im = ilGenImage();
        Images[i] = im;
    }
}


ILuint ILAPIENTRY ilGenImage()
{
    ILuint idx;
    if( stack_allocslot(&imageStack, &idx) == IL_FALSE) {
        return 0;
    }

    Slot* slot = slot_create();
    if(slot==NULL) {
        return 0;
    }

    imageStack.slots[idx] = slot;
    return idx;
}




//! Makes Image the current active image - similar to glBindTexture().
void ILAPIENTRY ilBindImage(ILuint Image)
{
    if(!ilIsImage(Image)) {
		ilSetError(IL_INVALID_VALUE);
		return;
    }

    // we can rely on first entry of slot being present
	iCurImage = imageStack.slots[Image]->images[0].img;
	CurName = Image;
    CurActive = slotid_ZERO;
}


//! Deletes Num images from the image stack - similar to glDeleteTextures().
void ILAPIENTRY ilDeleteImages(ILsizei Num, const ILuint *Images)
{
    int i;
    for (i=0; i<Num; ++i) {
        ilDeleteImage(Images[i]);
    }
}

void ILAPIENTRY ilDeleteImage(const ILuint Num) {
    // TODO: could prevent user from deleting slots 0 and 1 (default and temp)
    if(!ilIsImage(Num)) {
		ilSetError(IL_INVALID_VALUE);
		return;
    }
    slot_delete( imageStack.slots[Num] );
    imageStack.slots[Num] = NULL;
}

//! Checks if Image is a valid ilGenImages-generated image (like glIsTexture()).
ILboolean ILAPIENTRY ilIsImage(ILuint Image)
{
    if (Image>=imageStack.length) {
        return IL_FALSE;
    }
    if (imageStack.slots[Image]==NULL) {
        return IL_FALSE;
    }
    return IL_TRUE;
}


//! Closes Image and frees all memory associated with it.
ILAPI void ILAPIENTRY ilCloseImage(ILimage *Image)
{
	if (Image == NULL)
		return;

	if (Image->Data != NULL) {
		ifree(Image->Data);
		Image->Data = NULL;
	}

	if (Image->Pal.Palette != NULL && Image->Pal.PalSize > 0 && Image->Pal.PalType != IL_PAL_NONE) {
		ifree(Image->Pal.Palette);
		Image->Pal.Palette = NULL;
	}

	if (Image->Next != NULL) {
		ilCloseImage(Image->Next);
		Image->Next = NULL;
	}

	if (Image->Faces != NULL) {
		ilCloseImage(Image->Faces);
		Image->Mipmaps = NULL;
	}

	if (Image->Mipmaps != NULL) {
		ilCloseImage(Image->Mipmaps);
		Image->Mipmaps = NULL;
	}

	if (Image->Layers != NULL) {
		ilCloseImage(Image->Layers);
		Image->Layers = NULL;
	}

	if (Image->AnimList != NULL && Image->AnimSize != 0) {
		ifree(Image->AnimList);
		Image->AnimList = NULL;
	}

	if (Image->Profile != NULL && Image->ProfileSize != 0) {
		ifree(Image->Profile);
		Image->Profile = NULL;
		Image->ProfileSize = 0;
	}

	if (Image->DxtcData != NULL && Image->DxtcFormat != IL_DXT_NO_COMP) {
		ifree(Image->DxtcData);
		Image->DxtcData = NULL;
		Image->DxtcFormat = IL_DXT_NO_COMP;
		Image->DxtcSize = 0;
	}

	ifree(Image);
	Image = NULL;

	return;
}


ILAPI ILboolean ILAPIENTRY ilIsValidPal(ILpal *Palette)
{
	if (Palette == NULL)
		return IL_FALSE;
	if (Palette->PalSize == 0 || Palette->Palette == NULL)
		return IL_FALSE;
	switch (Palette->PalType)
	{
		case IL_PAL_RGB24:
		case IL_PAL_RGB32:
		case IL_PAL_RGBA32:
		case IL_PAL_BGR24:
		case IL_PAL_BGR32:
		case IL_PAL_BGRA32:
			return IL_TRUE;
	}
	return IL_FALSE;
}


//! Closes Palette and frees all memory associated with it.
ILAPI void ILAPIENTRY ilClosePal(ILpal *Palette)
{
	if (Palette == NULL)
		return;
	if (!ilIsValidPal(Palette))
		return;
	ifree(Palette->Palette);
	ifree(Palette);
	return;
}


// only used by il_state.c
// TODO: kill kill kill!
ILimage *iGetBaseImage()
{
	return imageStack.slots[ilGetCurName()]->images[0].img;
}


//! Sets the current mipmap level
ILboolean ILAPIENTRY ilActiveMipmap(ILuint Number)
{
    return ilActive(CurActive.frame, CurActive.mipmap+Number, CurActive.layer, CurActive.face);
}



//! Used for setting the current image if it is an animation.
ILboolean ILAPIENTRY ilActiveImage(ILuint Number)
{
    return ilActive(CurActive.frame+Number, CurActive.mipmap, CurActive.layer, CurActive.face);
}


//! Used for setting the current face if it is a cubemap.
ILboolean ILAPIENTRY ilActiveFace(ILuint Number)
{
    return ilActive(CurActive.frame, CurActive.mipmap, CurActive.layer, CurActive.face+Number);
}




//! Used for setting the current layer if layers exist.
ILboolean ILAPIENTRY ilActiveLayer(ILuint Number)
{
    return ilActive(CurActive.frame, CurActive.mipmap, CurActive.layer+Number, CurActive.face);
}


ILboolean ILAPIENTRY ilActive(ILuint frame, ILuint mipmap, ILuint layer, ILuint face)
{
    SlotID id = {frame,mipmap,layer,face};
    Slot* slot = imageStack.slots[CurName];
    int idx = slot_findimage(slot,id);
    if (idx==-1) {
        // don't have this one - need to add it
        idx = slot_add(slot,id);
        if (idx==-1) {
            return IL_FALSE;
        }

    }

    iCurImage = slot->images[idx].img;
}




// TODO: deprecated?
ILuint ILAPIENTRY ilCreateSubImage(ILenum Type, ILuint Num)
{
	ILimage	*SubImage;
	ILuint	Count ;  // Create one before we go in the loop.

	if (iCurImage == NULL) {
		ilSetError(IL_ILLEGAL_OPERATION);
		return 0;
	}
	if (Num == 0)  {
		return 0;
	}

	switch (Type)
	{
		case IL_SUB_NEXT:
			if (iCurImage->Next)
				ilCloseImage(iCurImage->Next);
			iCurImage->Next = ilNewImage(1, 1, 1, 1, 1);
			SubImage = iCurImage->Next;
			break;

		case IL_SUB_MIPMAP:
			if (iCurImage->Mipmaps)
				ilCloseImage(iCurImage->Mipmaps);
			iCurImage->Mipmaps = ilNewImage(1, 1, 1, 1, 1);
			SubImage = iCurImage->Mipmaps;
			break;

		case IL_SUB_LAYER:
			if (iCurImage->Layers)
				ilCloseImage(iCurImage->Layers);
			iCurImage->Layers = ilNewImage(1, 1, 1, 1, 1);
			SubImage = iCurImage->Layers;
			break;

		default:
			ilSetError(IL_INVALID_ENUM);
			return IL_FALSE;
	}

	if (SubImage == NULL) {
		return 0;
	}

	for (Count = 1; Count < Num; Count++) {
		SubImage->Next = ilNewImage(1, 1, 1, 1, 1);
		SubImage = SubImage->Next;
		if (SubImage == NULL)
			return Count;
	}

	return Count;
}


// Returns the current index.
ILAPI ILuint ILAPIENTRY ilGetCurName()
{
	return CurName;
}


// Returns the current image.
// (TODO: probably just remove this - _everywhere_ just uses iCurImage directly)
ILAPI ILimage* ILAPIENTRY ilGetCurImage()
{
	return iCurImage;
}


// To be only used when the original image is going to be set back almost immediately.
// TODO: make sure this plays nice with stack?
ILAPI void ILAPIENTRY ilSetCurImage(ILimage *Image)
{
	iCurImage = Image;
}

//TODO
// Completely replaces the current image and the version in the image stack.
ILAPI void ILAPIENTRY ilReplaceCurImage(ILimage *Image)
{
    Slot* slot = imageStack.slots[CurName];
    int idx;
	if (iCurImage) {
		ilCloseImage(iCurImage);
	}

    idx = slot_findimage(slot, CurActive); 
    // assert(idx>=0);
	imageStack.slots[ilGetCurName()]->images[idx].img = Image;
	iCurImage = Image;
}





// ONLY call at startup.
void ILAPIENTRY ilInit()
{
	// if it is already initialized skip initialization
	if (IsInit == IL_TRUE ) 
		return;

	//ilSetMemory(NULL, NULL);  Now useless 3/4/2006 (due to modification in il_alloc.c)
	ilSetError(IL_NO_ERROR);
	ilDefaultStates();  // Set states to their defaults.
	// Sets default file-reading callbacks.
	ilResetRead();
	ilResetWrite();
#if (!defined(_WIN32_WCE)) && (!defined(IL_STATIC_LIB))
	atexit((void*)ilRemoveRegistered);
#endif
    if (!stack_init(&imageStack)) {
        return;
    }
	//_WIN32_WCE
	IsInit = IL_TRUE;
	return;
}


// Frees any extra memory in the stack.
//	- Called on exit
void ILAPIENTRY ilShutDown()
{
	// if it is not initialized do not shutdown
	ILuint i;
	
	if (!IsInit) {  // Prevent from being called when not initialized.
		ilSetError(IL_ILLEGAL_OPERATION);
		return;
	}

    stack_cleanup(&imageStack);
    // TODO: tidy up CurName, iCurImage, CurActive vars?
	IsInit = IL_FALSE;
}



ILAPI void ILAPIENTRY iBindImageTemp()
{
    ilBindImage(1);
}
