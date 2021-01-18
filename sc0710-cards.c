#include "sc0710.h"

struct sc0710_board sc0710_boards[] = {
	[SC0710_BOARD_UNKNOWN] = {
		.name		= "UNKNOWN/GENERIC",
		/* Ensure safe default for unknown boards */
	},
	[SC0710_BOARD_ELGATEO_4KP60_MK2] = {
		.name		= "Elgato 4k60 Pro mk.2",
	},
};
const unsigned int sc0710_bcount = ARRAY_SIZE(sc0710_boards);

struct sc0710_subid sc0710_subids[] = {
	{
		.subvendor = 0x1cfa,
		.subdevice = 0x000e,
		.card      = SC0710_BOARD_ELGATEO_4KP60_MK2,
	}
};
const unsigned int sc0710_idcount = ARRAY_SIZE(sc0710_subids);

void sc0710_card_list(struct sc0710_dev *dev)
{
	int i;

	if (0 == dev->pci->subsystem_vendor &&
	    0 == dev->pci->subsystem_device) {
		printk(KERN_INFO
			"%s: Board has no valid PCIe Subsystem ID and can't\n"
		       "%s: be autodetected. Pass card=<n> insmod option\n"
		       "%s: to workaround that. Redirect complaints to the\n"
		       "%s: vendor of the TV card.  Best regards,\n"
		       "%s:         -- tux\n",
		       dev->name, dev->name, dev->name, dev->name, dev->name);
	} else {
		printk(KERN_INFO
			"%s: Your board isn't known (yet) to the driver.\n"
		       "%s: Try to pick one of the existing card configs via\n"
		       "%s: card=<n> insmod option.  Updating to the latest\n"
		       "%s: version might help as well.\n",
		       dev->name, dev->name, dev->name, dev->name);
	}
	printk(KERN_INFO "%s: Here is a list of valid choices for the card=<n> insmod option:\n",
	       dev->name);
	for (i = 0; i < sc0710_bcount; i++)
		printk(KERN_INFO "%s:    card=%d -> %s\n",
		       dev->name, i, sc0710_boards[i].name);
}

void sc0710_gpio_setup(struct sc0710_dev *dev)
{
	switch (dev->board) {
	case SC0710_BOARD_ELGATEO_4KP60_MK2:
		break;
	}
}

void sc0710_card_setup(struct sc0710_dev *dev)
{
	switch (dev->board) {
	case SC0710_BOARD_ELGATEO_4KP60_MK2:
		break;
	}
}

