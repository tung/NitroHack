/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* NitroHack may be freely redistributed.  See license for details. */

#include "hack.h"

/* croom->lx etc are schar (width <= int), so % arith ensures that */
/* conversion of result to int is reasonable */

static void mkfount(struct level *lev, int,struct mkroom *);
static void mksink(struct level *lev, struct mkroom *);
static void mkaltar(struct level *lev, struct mkroom *);
static void mkgrave(struct level *lev, struct mkroom *);
static void makevtele(struct level *lev);
static void makelevel(struct level *lev);
static void mineralize(struct level *lev);
static boolean bydoor(struct level *lev, xchar,xchar);
static struct mkroom *find_branch_room(struct level *lev, coord *);
static struct mkroom *pos_to_room(struct level *lev, xchar x, xchar y);
static boolean place_niche(struct level *lev, struct mkroom *,int*,int*,int*);
static void makeniche(struct level *lev, int);
static void make_niches(struct level *lev);

static int do_comp(const void *,const void *);

static void dosdoor(struct level *lev, xchar,xchar,struct mkroom *,int);
static void join(struct level *lev, int a,int b, boolean);
static void do_room_or_subroom(struct level *lev, struct mkroom *,int,int,int,int,
				       boolean,schar,boolean,boolean);
static void makerooms(struct level *lev);
static void finddpos(struct level *lev, coord *,xchar,xchar,xchar,xchar);
static void mkinvpos(xchar,xchar,int);
static void mk_knox_portal(struct level *lev, xchar,xchar);
static void place_random_engravings(struct level *lev);

#define create_vault(lev)	create_room(lev, -1, -1, 2, 2, -1, -1, VAULT, TRUE)
#define do_vault()	(vault_x != -1)
static xchar		vault_x, vault_y;
boolean goldseen;
static boolean made_branch;	/* used only during level creation */

/* Args must be (const void *) so that qsort will always be happy. */

static int do_comp(const void *vx, const void *vy)
{
	const struct mkroom *x, *y;

	x = (const struct mkroom *)vx;
	y = (const struct mkroom *)vy;

	/* sort by x coord first */
	if (x->lx != y->lx)
	    return x->lx - y->lx;

	/* sort by ly if lx is equal
	 * The additional criterium is necessary to get consistent sorting across
	 * platforms with different qsort implementations. */
	return x->ly - y->ly;
}

static void finddpos(struct level *lev, coord *cc, xchar xl, xchar yl, xchar xh, xchar yh)
{
	xchar x, y;

	x = (xl == xh) ? xl : (xl + rn2(xh-xl+1));
	y = (yl == yh) ? yl : (yl + rn2(yh-yl+1));
	if (okdoor(lev, x, y))
		goto gotit;

	for (x = xl; x <= xh; x++) for(y = yl; y <= yh; y++)
		if (okdoor(lev, x, y))
			goto gotit;

	for (x = xl; x <= xh; x++) for(y = yl; y <= yh; y++)
		if (IS_DOOR(lev->locations[x][y].typ) || lev->locations[x][y].typ == SDOOR)
			goto gotit;
	/* cannot find something reasonable -- strange */
	x = xl;
	y = yh;
gotit:
	cc->x = x;
	cc->y = y;
	return;
}

void sort_rooms(struct level *lev)
{
	qsort(lev->rooms, lev->nroom, sizeof(struct mkroom), do_comp);
}

static void do_room_or_subroom(struct level *lev,
			       struct mkroom *croom,
			       int lowx, int lowy,
			       int hix, int hiy,
			       boolean lit,
			       schar rtype,
			       boolean special,
			       boolean is_room)
{
	int x, y;
	struct rm *loc;

	/* locations might bump level edges in wall-less rooms */
	/* add/subtract 1 to allow for edge locations */
	if (!lowx) lowx++;
	if (!lowy) lowy++;
	if (hix >= COLNO-1) hix = COLNO-2;
	if (hiy >= ROWNO-1) hiy = ROWNO-2;

	if (lit) {
		for (x = lowx-1; x <= hix+1; x++) {
			loc = &lev->locations[x][max(lowy-1,0)];
			for (y = lowy-1; y <= hiy+1; y++)
				loc++->lit = 1;
		}
		croom->rlit = 1;
	} else
		croom->rlit = 0;

	croom->lx = lowx;
	croom->hx = hix;
	croom->ly = lowy;
	croom->hy = hiy;
	croom->rtype = rtype;
	croom->doorct = 0;
	/* if we're not making a vault, lev->doorindex will still be 0
	 * if we are, we'll have problems adding niches to the previous room
	 * unless fdoor is at least doorindex
	 */
	croom->fdoor = lev->doorindex;
	croom->irregular = FALSE;

	croom->nsubrooms = 0;
	croom->sbrooms[0] = NULL;
	if (!special) {
	    for (x = lowx-1; x <= hix+1; x++)
		for (y = lowy-1; y <= hiy+1; y += (hiy-lowy+2)) {
		    lev->locations[x][y].typ = HWALL;
		    lev->locations[x][y].horizontal = 1;	/* For open/secret doors. */
		}
	    for (x = lowx-1; x <= hix+1; x += (hix-lowx+2))
		for (y = lowy; y <= hiy; y++) {
		    lev->locations[x][y].typ = VWALL;
		    lev->locations[x][y].horizontal = 0;	/* For open/secret doors. */
		}
	    for (x = lowx; x <= hix; x++) {
		loc = &lev->locations[x][lowy];
		for (y = lowy; y <= hiy; y++)
		    loc++->typ = ROOM;
	    }
	    if (is_room) {
		lev->locations[lowx-1][lowy-1].typ = TLCORNER;
		lev->locations[hix+1][lowy-1].typ = TRCORNER;
		lev->locations[lowx-1][hiy+1].typ = BLCORNER;
		lev->locations[hix+1][hiy+1].typ = BRCORNER;
	    } else {	/* a subroom */
		wallification(lev, lowx-1, lowy-1, hix+1, hiy+1);
	    }
	}
}


void add_room(struct level *lev, int lowx, int lowy, int hix, int hiy, boolean lit,
	      schar rtype, boolean special)
{
	struct mkroom *croom;

	croom = &lev->rooms[lev->nroom];
	do_room_or_subroom(lev, croom, lowx, lowy, hix, hiy, lit,
					    rtype, special, (boolean) TRUE);
	croom++;
	croom->hx = -1;
	lev->nroom++;
}

void add_subroom(struct level *lev, struct mkroom *proom, int lowx, int lowy,
		 int hix, int hiy, boolean lit, schar rtype, boolean special)
{
	struct mkroom *croom;

	croom = &lev->subrooms[lev->nsubroom];
	do_room_or_subroom(lev, croom, lowx, lowy, hix, hiy, lit,
					    rtype, special, FALSE);
	proom->sbrooms[proom->nsubrooms++] = croom;
	croom++;
	croom->hx = -1;
	lev->nsubroom++;
}

static void makerooms(struct level *lev)
{
	boolean tried_vault = FALSE;

	/* make rooms until satisfied */
	/* rnd_rect() will returns 0 if no more rects are available... */
	while (lev->nroom < MAXNROFROOMS && rnd_rect()) {
		if (lev->nroom >= (MAXNROFROOMS/6) && rn2(2) && !tried_vault) {
			tried_vault = TRUE;
			if (create_vault(lev)) {
				vault_x = lev->rooms[lev->nroom].lx;
				vault_y = lev->rooms[lev->nroom].ly;
				lev->rooms[lev->nroom].hx = -1;
			}
		} else
		    if (!create_room(lev, -1, -1, -1, -1, -1, -1, OROOM, -1))
			return;
	}
	return;
}

static void join(struct level *lev, int a, int b, boolean nxcor)
{
	coord cc,tt, org, dest;
	xchar tx, ty, xx, yy;
	struct mkroom *croom, *troom;
	int dx, dy;

	croom = &lev->rooms[a];
	troom = &lev->rooms[b];

	/* find positions cc and tt for doors in croom and troom
	   and direction for a corridor between them */

	if (troom->hx < 0 || croom->hx < 0 || lev->doorindex >= DOORMAX) return;
	if (troom->lx > croom->hx) {
		dx = 1;
		dy = 0;
		xx = croom->hx+1;
		tx = troom->lx-1;
		finddpos(lev, &cc, xx, croom->ly, xx, croom->hy);
		finddpos(lev, &tt, tx, troom->ly, tx, troom->hy);
	} else if (troom->hy < croom->ly) {
		dy = -1;
		dx = 0;
		yy = croom->ly-1;
		finddpos(lev, &cc, croom->lx, yy, croom->hx, yy);
		ty = troom->hy+1;
		finddpos(lev, &tt, troom->lx, ty, troom->hx, ty);
	} else if (troom->hx < croom->lx) {
		dx = -1;
		dy = 0;
		xx = croom->lx-1;
		tx = troom->hx+1;
		finddpos(lev, &cc, xx, croom->ly, xx, croom->hy);
		finddpos(lev, &tt, tx, troom->ly, tx, troom->hy);
	} else {
		dy = 1;
		dx = 0;
		yy = croom->hy+1;
		ty = troom->ly-1;
		finddpos(lev, &cc, croom->lx, yy, croom->hx, yy);
		finddpos(lev, &tt, troom->lx, ty, troom->hx, ty);
	}
	xx = cc.x;
	yy = cc.y;
	tx = tt.x - dx;
	ty = tt.y - dy;
	if (nxcor && lev->locations[xx+dx][yy+dy].typ)
		return;
	if (okdoor(lev, xx, yy) || !nxcor)
	    dodoor(lev, xx, yy, croom);

	org.x  = xx+dx; org.y  = yy+dy;
	dest.x = tx; dest.y = ty;

	if (!dig_corridor(lev, &org, &dest, nxcor,
			lev->flags.arboreal ? ROOM : CORR, STONE))
	    return;

	/* we succeeded in digging the corridor */
	if (okdoor(lev, tt.x, tt.y) || !nxcor)
	    dodoor(lev, tt.x, tt.y, troom);

	if (smeq[a] < smeq[b])
		smeq[b] = smeq[a];
	else
		smeq[a] = smeq[b];
}

void makecorridors(struct level *lev)
{
	int a, b, i;
	boolean any = TRUE;

	for (a = 0; a < lev->nroom-1; a++) {
		join(lev, a, a+1, FALSE);
		if (!rn2(50)) break; /* allow some randomness */
	}
	for (a = 0; a < lev->nroom-2; a++)
	    if (smeq[a] != smeq[a+2])
		join(lev, a, a+2, FALSE);
	for (a = 0; any && a < lev->nroom; a++) {
	    any = FALSE;
	    for (b = 0; b < lev->nroom; b++)
		if (smeq[a] != smeq[b]) {
		    join(lev, a, b, FALSE);
		    any = TRUE;
		}
	}
	if (lev->nroom > 2)
	    for (i = rn2(lev->nroom) + 4; i; i--) {
		a = rn2(lev->nroom);
		b = rn2(lev->nroom-2);
		if (b >= a) b += 2;
		join(lev, a, b, TRUE);
	    }
}

void add_door(struct level *lev, int x, int y, struct mkroom *aroom)
{
	struct mkroom *broom;
	int tmp;

	aroom->doorct++;
	broom = aroom+1;
	if (broom->hx < 0)
		tmp = lev->doorindex;
	else
		for (tmp = lev->doorindex; tmp > broom->fdoor; tmp--)
			lev->doors[tmp] = lev->doors[tmp-1];
	lev->doorindex++;
	lev->doors[tmp].x = x;
	lev->doors[tmp].y = y;
	for ( ; broom->hx >= 0; broom++) broom->fdoor++;
}

static void dosdoor(struct level *lev, xchar x, xchar y, struct mkroom *aroom, int type)
{
	boolean shdoor = ((*in_rooms(lev, x, y, SHOPBASE))? TRUE : FALSE);

	if (!IS_WALL(lev->locations[x][y].typ)) /* avoid SDOORs on already made doors */
		type = DOOR;
	lev->locations[x][y].typ = type;
	if (type == DOOR) {
	    if (!rn2(3)) {      /* is it a locked door, closed, or a doorway? */
		if (!rn2(5))
		    lev->locations[x][y].doormask = D_ISOPEN;
		else if (!rn2(6))
		    lev->locations[x][y].doormask = D_LOCKED;
		else
		    lev->locations[x][y].doormask = D_CLOSED;

		if (lev->locations[x][y].doormask != D_ISOPEN && !shdoor &&
		    level_difficulty(&lev->z) >= 5 && !rn2(25))
		    lev->locations[x][y].doormask |= D_TRAPPED;
	    } else
		lev->locations[x][y].doormask = (shdoor ? D_ISOPEN : D_NODOOR);
	    if (lev->locations[x][y].doormask & D_TRAPPED) {
		struct monst *mtmp;

		if (level_difficulty(&lev->z) >= 9 && !rn2(5) &&
		   !((mvitals[PM_SMALL_MIMIC].mvflags & G_GONE) &&
		     (mvitals[PM_LARGE_MIMIC].mvflags & G_GONE) &&
		     (mvitals[PM_GIANT_MIMIC].mvflags & G_GONE))) {
		    /* make a mimic instead */
		    lev->locations[x][y].doormask = D_NODOOR;
		    mtmp = makemon(mkclass(&lev->z, S_MIMIC,0), lev, x, y, NO_MM_FLAGS);
		    if (mtmp)
			set_mimic_sym(mtmp, lev);
		}
	    }
	    /* newsym(x,y); */
	} else { /* SDOOR */
		if (shdoor || !rn2(5))	lev->locations[x][y].doormask = D_LOCKED;
		else			lev->locations[x][y].doormask = D_CLOSED;

		if (!shdoor && level_difficulty(&lev->z) >= 4 && !rn2(20))
		    lev->locations[x][y].doormask |= D_TRAPPED;
	}

	add_door(lev, x, y, aroom);
}

static boolean place_niche(struct level *lev, struct mkroom *aroom,
			   int *dy, int *xx, int *yy)
{
	coord dd;

	if (rn2(2)) {
	    *dy = 1;
	    finddpos(lev, &dd, aroom->lx, aroom->hy+1, aroom->hx, aroom->hy+1);
	} else {
	    *dy = -1;
	    finddpos(lev, &dd, aroom->lx, aroom->ly-1, aroom->hx, aroom->ly-1);
	}
	*xx = dd.x;
	*yy = dd.y;
	return((boolean)((isok(*xx,*yy+*dy) && lev->locations[*xx][*yy+*dy].typ == STONE)
	    && (isok(*xx,*yy-*dy) && !IS_POOL(lev->locations[*xx][*yy-*dy].typ)
				  && !IS_FURNITURE(lev->locations[*xx][*yy-*dy].typ))));
}

/* there should be one of these per trap, in the same order as trap.h */
static const char * const trap_engravings[TRAPNUM] = {
			NULL, NULL, NULL, NULL, NULL,
			NULL, NULL, NULL, NULL, NULL,
			NULL, NULL, NULL, NULL,
			/* 14..17: trap door, VS, teleport, level-teleport */
			"Vlad was here", NULL, "ad aerarium", "ad aerarium",
			NULL, NULL, NULL, NULL, NULL,
			NULL,
};

/* Quotes from or about the Illuminati */
static const char * const illuminati_engravings[] = {
	"Ewige Blumenkraft!", /* the Illuminatus' motto */
	"Ewige Blumenkraft Und Ewige Schlangenkraft", /* the complete Illuminatus' motto */
	"fnord", /* you can't see it */
	"FUCKUP calculates your odds of ascending as negligibly small",
	"Hail Eris!",
	"Jeder Gedanke ist verschenkt, wenn Adam Weishaupt fuer dich denkt", /* Welle:Erdball */
	"Novus Ordo Seclorum",
	"Think for yourself, schmuck",
};

/* Quotes from or about the Hitchhikers Guide to the Galaxy */
static const char * const hhgtg_engravings[] = {
	"Beeblebrox for President!",
	"Don't Panic",
	"Got your towel?",
	"Time is an illusion. Lunchtime doubly so.",
	"Oh no, not again.",
	"Resistance is useless!",
	"Slartibartfast was here!",
	"So long, and thanks for all the fish",
	"Visit Milliways, the Restaurant at the End of the Universe!",
	"We apologize for the inconvenience",
};

static void makeniche(struct level *lev, int trap_type)
{
	struct mkroom *aroom;
	struct rm *rm;
	int vct = 8;
	int dy, xx, yy;
	struct trap *ttmp;

	if (lev->doorindex < DOORMAX)
	  while (vct--) {
	    aroom = &lev->rooms[rn2(lev->nroom)];
	    if (aroom->rtype != OROOM) continue;	/* not an ordinary room */
	    if (aroom->doorct == 1 && rn2(5)) continue;
	    if (!place_niche(lev, aroom, &dy, &xx, &yy)) continue;

	    rm = &lev->locations[xx][yy+dy];
	    if (trap_type || !rn2(4)) {

		rm->typ = SCORR;
		if (trap_type) {
		    if ((trap_type == HOLE || trap_type == TRAPDOOR)
			&& !can_fall_thru(lev))
			trap_type = ROCKTRAP;
		    ttmp = maketrap(lev, xx, yy+dy, trap_type);
		    if (ttmp) {
			if (trap_type != ROCKTRAP) ttmp->once = 1;
			if (trap_engravings[trap_type]) {
			    make_engr_at(lev, xx, yy-dy,
				     trap_engravings[trap_type], 0L, DUST);
			    wipe_engr_at(lev, xx, yy-dy, 5); /* age it a little */
			}
		    }
		}
		dosdoor(lev, xx, yy, aroom, SDOOR);
	    } else {
		rm->typ = CORR;
		if (rn2(7))
		    dosdoor(lev, xx, yy, aroom, rn2(5) ? SDOOR : DOOR);
		else {
		    if (!lev->flags.noteleport)
			mksobj_at(SCR_TELEPORTATION, lev,
					 xx, yy+dy, TRUE, FALSE);
		    if (!rn2(3)) mkobj_at(0, lev, xx, yy+dy, TRUE);
		}
	    }
	    return;
	}
}

static void make_niches(struct level *lev)
{
	int ct = rnd((lev->nroom>>1) + 1), dep = depth(&lev->z);

	boolean	ltptr = (!lev->flags.noteleport && dep > 15),
		vamp = (dep > 5 && dep < 25);

	while (ct--) {
		if (ltptr && !rn2(6)) {
			ltptr = FALSE;
			makeniche(lev, LEVEL_TELEP);
		} else if (vamp && !rn2(6)) {
			vamp = FALSE;
			makeniche(lev, TRAPDOOR);
		} else	makeniche(lev, NO_TRAP);
	}
}

static void makevtele(struct level *lev)
{
	makeniche(lev, TELEP_TRAP);
}

/* Allocate a new level structure and make sure its fields contain sane
 * initial vales.
 * Some of this is only necessary for some types of levels (maze, normal,
 * special) but it's easier to put it all in one place than make sure
 * each type initializes what it needs to separately.
 */
struct level *alloc_level(d_level *levnum)
{
	struct level *lev = malloc(sizeof(struct level));
	
	memset(lev, 0, sizeof(struct level));
	if (levnum)
	    lev->z = *levnum;
	lev->subrooms = &lev->rooms[MAXNROFROOMS+1]; /* compat */
	lev->rooms[0].hx = -1;
	lev->subrooms[0].hx = -1;
	lev->flags.hero_memory = 1;

	/* these are not part of the level structure, but are obly used while
	 * making new levels */
	init_rect();
	vault_x = -1;
	made_branch = FALSE;

	return lev;
}

static void makelevel(struct level *lev)
{
	struct mkroom *croom, *troom;
	int tryct;
	int x, y;
	struct monst *tmonst;	/* always put a web with a spider */
	branch *branchp;
	int room_threshold;

	if (wiz1_level.dlevel == 0) init_dungeons();
	oinit(lev);	/* assign level dependent obj probabilities */

	{
	    s_level *slevnum = Is_special(&lev->z);

	    /* check for special levels */
	    if (slevnum && !Is_rogue_level(&lev->z)) {
		    makemaz(lev, slevnum->proto);
		    return;
	    } else if (dungeons[lev->z.dnum].proto[0]) {
		    makemaz(lev, "");
		    return;
	    } else if (In_mines(&lev->z)) {
		    makemaz(lev, "minefill");
		    return;
	    } else if (In_quest(&lev->z)) {
		    char	fillname[9];
		    s_level	*loc_levnum;

		    sprintf(fillname, "%s-loca", urole.filecode);
		    loc_levnum = find_level(fillname);

		    sprintf(fillname, "%s-fil", urole.filecode);
		    strcat(fillname,
			   (lev->z.dlevel < loc_levnum->dlevel.dlevel) ? "a" : "b");
		    makemaz(lev, fillname);
		    return;
	    } else if (In_hell(&lev->z) ||
		  (rn2(5) && lev->z.dnum == medusa_level.dnum
			  && depth(&lev->z) > depth(&medusa_level))) {
		    makemaz(lev, "");
		    return;
	    }
	}

	/* otherwise, fall through - it's a "regular" level. */

	if (Is_rogue_level(&lev->z)) {
		makeroguerooms(lev);
		makerogueghost(lev);
	} else
		makerooms(lev);
	sort_rooms(lev);

	/* construct stairs (up and down in different rooms if possible) */
	croom = &lev->rooms[rn2(lev->nroom)];
	if (!Is_botlevel(&lev->z))
	     mkstairs(lev, somex(croom), somey(croom), 0, croom); /* down */
	if (lev->nroom > 1) {
	    troom = croom;
	    croom = &lev->rooms[rn2(lev->nroom-1)];
	    if (croom == troom) croom++;
	}

	if (lev->z.dlevel != 1) {
	    xchar sx, sy;
	    do {
		sx = somex(croom);
		sy = somey(croom);
	    } while (occupied(lev, sx, sy));
	    mkstairs(lev, sx, sy, 1, croom);	/* up */
	}

	branchp = Is_branchlev(&lev->z);	/* possible dungeon branch */
	room_threshold = branchp ? 4 : 3; /* minimum number of rooms needed
					     to allow a random special room */
	if (Is_rogue_level(&lev->z))
	    goto skip0;
	makecorridors(lev);
	make_niches(lev);

	/* make a secret treasure vault, not connected to the rest */
	if (do_vault()) {
		xchar w,h;
		w = 1;
		h = 1;
		if (check_room(lev, &vault_x, &w, &vault_y, &h, TRUE)) {
		    fill_vault:
			add_room(lev, vault_x, vault_y, vault_x+w,
				 vault_y+h, TRUE, VAULT, FALSE);
			lev->flags.has_vault = 1;
			++room_threshold;
			fill_room(lev, &lev->rooms[lev->nroom - 1], FALSE);
			mk_knox_portal(lev, vault_x+w, vault_y+h);
			if (!lev->flags.noteleport && !rn2(3)) makevtele(lev);
		} else if (rnd_rect() && create_vault(lev)) {
			vault_x = lev->rooms[lev->nroom].lx;
			vault_y = lev->rooms[lev->nroom].ly;
			if (check_room(lev, &vault_x, &w, &vault_y, &h, TRUE))
				goto fill_vault;
			else
				lev->rooms[lev->nroom].hx = -1;
		}
	}

    {
	int u_depth = depth(&lev->z);

	if (u_depth > 1 &&
	    u_depth < depth(&medusa_level) &&
	    lev->nroom >= room_threshold &&
	    rn2(u_depth) < 3) mkroom(lev, SHOPBASE);
	else if (u_depth > 4 && !rn2(6)) mkroom(lev, COURT);
	else if (u_depth > 5 && !rn2(8) &&
	   !(mvitals[PM_LEPRECHAUN].mvflags & G_GONE)) mkroom(lev, LEPREHALL);
	else if (u_depth > 6 && !rn2(7)) mkroom(lev, ZOO);
	else if (u_depth > 7 && !rn2(6)) mkroom(lev, GARDEN);
	else if (u_depth > 8 && !rn2(5)) mkroom(lev, TEMPLE);
	else if (u_depth > 9 && !rn2(5) &&
	   !(mvitals[PM_KILLER_BEE].mvflags & G_GONE)) mkroom(lev, BEEHIVE);
	else if (u_depth > 11 && !rn2(6)) mkroom(lev, MORGUE);
	else if (u_depth > 12 && !rn2(8)) mkroom(lev, ANTHOLE);
	else if (u_depth > 14 && !rn2(4) &&
	   !(mvitals[PM_SOLDIER].mvflags & G_GONE)) mkroom(lev, BARRACKS);
	else if (u_depth > 15 && !rn2(6)) mkroom(lev, SWAMP);
	else if (u_depth > 16 && !rn2(8) &&
	   !(mvitals[PM_COCKATRICE].mvflags & G_GONE)) mkroom(lev, COCKNEST);
    }

skip0:
	/* Place multi-dungeon branch. */
	place_branch(lev, branchp, 0, 0);

	/* for each room: put things inside */
	for (croom = lev->rooms; croom->hx > 0; croom++) {
		if (croom->rtype != OROOM) continue;

		/* put a sleeping monster inside */
		/* Note: monster may be on the stairs. This cannot be
		   avoided: maybe the player fell through a trap door
		   while a monster was on the stairs. Conclusion:
		   we have to check for monsters on the stairs anyway. */

		if (u.uhave.amulet || !rn2(3)) {
		    x = somex(croom); y = somey(croom);
		    tmonst = makemon(NULL, lev, x, y, NO_MM_FLAGS);
		    if (tmonst && tmonst->data == &mons[PM_GIANT_SPIDER] &&
			    !occupied(lev, x, y))
			maketrap(lev, x, y, WEB);
		}
		/* put traps and mimics inside */
		goldseen = FALSE;
		x = 8 - (level_difficulty(&lev->z)/6);
		if (x <= 1) x = 2;
		while (!rn2(x))
		    mktrap(lev, 0, 0, croom, NULL);
		if (!goldseen && !rn2(3))
		    mkgold(0L, lev, somex(croom), somey(croom));
		if (Is_rogue_level(&lev->z)) goto skip_nonrogue;
		if (!rn2(10)) mkfount(lev, 0, croom);
		if (!rn2(60)) mksink(lev, croom);
		if (!rn2(60)) mkaltar(lev, croom);
		x = 80 - (depth(&lev->z) * 2);
		if (x < 2) x = 2;
		if (!rn2(x)) mkgrave(lev, croom);

		/* put statues inside */
		if (!rn2(20))
		    mkcorpstat(STATUE, NULL, NULL, lev,
				      somex(croom), somey(croom), TRUE);
		/* put box/chest inside;
		 *  40% chance for at least 1 box, regardless of number
		 *  of rooms; about 5 - 7.5% for 2 boxes, least likely
		 *  when few rooms; chance for 3 or more is neglible.
		 */
		if (!rn2(lev->nroom * 5 / 2))
		    mksobj_at((rn2(3)) ? LARGE_BOX : CHEST, lev,
				     somex(croom), somey(croom), TRUE, FALSE);

		/* maybe make some graffiti */
		if (!rn2(27 + 3 * abs(depth(&lev->z)))) {
		    char buf[BUFSZ];
		    const char *mesg = random_engraving(buf);
		    if (mesg) {
			do {
			    x = somex(croom);  y = somey(croom);
			} while (lev->locations[x][y].typ != ROOM && !rn2(40));
			if (!(IS_POOL(lev->locations[x][y].typ) ||
			      IS_FURNITURE(lev->locations[x][y].typ)))
			    make_engr_at(lev, x, y, mesg, 0L, MARK);
		    }
		}

	skip_nonrogue:
		if (!rn2(3)) {
		    mkobj_at(0, lev, somex(croom), somey(croom), TRUE);
		    tryct = 0;
		    while (!rn2(5)) {
			if (++tryct > 100) {
			    impossible("tryct overflow4");
			    break;
			}
			mkobj_at(0, lev, somex(croom), somey(croom), TRUE);
		    }
		}
	}
}

/*
 *	Place deposits of minerals (gold and misc gems) in the stone
 *	surrounding the rooms on the map.
 *	Also place kelp in water.
 */
static void mineralize(struct level *lev)
{
	s_level *sp;
	struct obj *otmp;
	int goldprob, gemprob, x, y, cnt;


	/* Place kelp, except on the plane of water */
	if (In_endgame(&lev->z)) return;
	for (x = 2; x < (COLNO - 2); x++)
	    for (y = 1; y < (ROWNO - 1); y++)
		if ((lev->locations[x][y].typ == POOL && !rn2(10)) ||
			(lev->locations[x][y].typ == MOAT && !rn2(30)))
		    mksobj_at(KELP_FROND, lev, x, y, TRUE, FALSE);

	/* determine if it is even allowed;
	   almost all special levels are excluded */
	if (In_hell(&lev->z) || In_V_tower(&lev->z) ||
		Is_rogue_level(&lev->z) ||
		lev->flags.arboreal ||
		((sp = Is_special(&lev->z)) != 0 && !Is_oracle_level(&lev->z)
					&& (!In_mines(&lev->z) || sp->flags.town)
	    )) return;

	/* basic level-related probabilities */
	goldprob = 20 + depth(&lev->z) / 3;
	gemprob = goldprob / 4;

	/* mines have ***MORE*** goodies - otherwise why mine? */
	if (In_mines(&lev->z)) {
	    goldprob *= 2;
	    gemprob *= 3;
	} else if (In_quest(&lev->z)) {
	    goldprob /= 4;
	    gemprob /= 6;
	}

	/*
	 * Seed rock areas with gold and/or gems.
	 * We use fairly low level object handling to avoid unnecessary
	 * overhead from placing things in the floor chain prior to burial.
	 */
	for (x = 2; x < (COLNO - 2); x++)
	  for (y = 1; y < (ROWNO - 1); y++)
	    if (lev->locations[x][y+1].typ != STONE) {	 /* <x,y> spot not eligible */
		y += 2;		/* next two spots aren't eligible either */
	    } else if (lev->locations[x][y].typ != STONE) { /* this spot not eligible */
		y += 1;		/* next spot isn't eligible either */
	    } else if (!(lev->locations[x][y].wall_info & W_NONDIGGABLE) &&
		  lev->locations[x][y-1].typ   == STONE &&
		  lev->locations[x+1][y-1].typ == STONE && lev->locations[x-1][y-1].typ == STONE &&
		  lev->locations[x+1][y].typ   == STONE && lev->locations[x-1][y].typ   == STONE &&
		  lev->locations[x+1][y+1].typ == STONE && lev->locations[x-1][y+1].typ == STONE) {
		if (rn2(1000) < goldprob) {
		    if ((otmp = mksobj(lev, GOLD_PIECE, FALSE, FALSE)) != 0) {
			otmp->ox = x,  otmp->oy = y;
			otmp->quan = 1L + rnd(goldprob * 3);
			otmp->owt = weight(otmp);
			if (!rn2(3)) add_to_buried(otmp);
			else place_object(otmp, lev, x, y);
		    }
		}
		if (rn2(1000) < gemprob) {
		    for (cnt = rnd(2 + dunlev(&lev->z) / 3); cnt > 0; cnt--)
			if ((otmp = mkobj(lev, GEM_CLASS, FALSE)) != 0) {
			    if (otmp->otyp == ROCK) {
				dealloc_obj(otmp);	/* discard it */
			    } else {
				otmp->ox = x,  otmp->oy = y;
				if (!rn2(3)) add_to_buried(otmp);
				else place_object(otmp, lev, x, y);
			    }
		    }
		}
	    }
}

struct level *mklev(d_level *levnum)
{
	struct mkroom *croom;
	int ln = ledger_no(levnum);
	struct level *lev;
	
	if (levels[ln])
	    return levels[ln];
	
	if (getbones(levnum))
	    return levels[ln]; /* initialized in getbones->getlev */
	
	lev = levels[ln] = alloc_level(levnum);

	in_mklev = TRUE;
	makelevel(lev);
	bound_digging(lev);
	mineralize(lev);
	place_random_engravings(lev);
	in_mklev = FALSE;
	/* has_morgue gets cleared once morgue is entered; graveyard stays
	   set (graveyard might already be set even when has_morgue is clear
	   [see fixup_special()], so don't update it unconditionally) */
	if (lev->flags.has_morgue)
	    lev->flags.graveyard = 1;
	if (!lev->flags.is_maze_lev) {
	    for (croom = &lev->rooms[0]; croom != &lev->rooms[lev->nroom]; croom++)
		topologize(lev, croom);
	}
	set_wall_state(lev);
	
	return lev;
}


void topologize(struct level *lev, struct mkroom *croom)
{
	int x, y, roomno = (croom - lev->rooms) + ROOMOFFSET;
	int lowx = croom->lx, lowy = croom->ly;
	int hix = croom->hx, hiy = croom->hy;
	int subindex, nsubrooms = croom->nsubrooms;

	/* skip the room if already done; i.e. a shop handled out of order */
	/* also skip if this is non-rectangular (it _must_ be done already) */
	if ((int) lev->locations[lowx][lowy].roomno == roomno || croom->irregular)
	    return;
	{
	    /* do innards first */
	    for (x = lowx; x <= hix; x++)
		for (y = lowy; y <= hiy; y++)
		    lev->locations[x][y].roomno = roomno;
	    /* top and bottom edges */
	    for (x = lowx-1; x <= hix+1; x++)
		for (y = lowy-1; y <= hiy+1; y += (hiy-lowy+2)) {
		    lev->locations[x][y].edge = 1;
		    if (lev->locations[x][y].roomno)
			lev->locations[x][y].roomno = SHARED;
		    else
			lev->locations[x][y].roomno = roomno;
		}
	    /* sides */
	    for (x = lowx-1; x <= hix+1; x += (hix-lowx+2))
		for (y = lowy; y <= hiy; y++) {
		    lev->locations[x][y].edge = 1;
		    if (lev->locations[x][y].roomno)
			lev->locations[x][y].roomno = SHARED;
		    else
			lev->locations[x][y].roomno = roomno;
		}
	}
	/* subrooms */
	for (subindex = 0; subindex < nsubrooms; subindex++)
		topologize(lev, croom->sbrooms[subindex]);
}

/* Find an unused room for a branch location. */
static struct mkroom *find_branch_room(struct level *lev, coord *mp)
{
    struct mkroom *croom = 0;

    if (lev->nroom == 0) {
	mazexy(lev, mp);	/* already verifies location */
    } else {
	/* not perfect - there may be only one stairway */
	if (lev->nroom > 2) {
	    int tryct = 0;

	    do
		croom = &lev->rooms[rn2(lev->nroom)];
	    while ((croom == lev->dnstairs_room || croom == lev->upstairs_room ||
		  croom->rtype != OROOM) && (++tryct < 100));
	} else
	    croom = &lev->rooms[rn2(lev->nroom)];

	do {
	    if (!somexy(lev, croom, mp))
		impossible("Can't place branch!");
	} while (occupied(lev, mp->x, mp->y) ||
	    (lev->locations[mp->x][mp->y].typ != CORR && lev->locations[mp->x][mp->y].typ != ROOM));
    }
    return croom;
}

/* Find the room for (x,y).  Return null if not in a room. */
static struct mkroom *pos_to_room(struct level *lev, xchar x, xchar y)
{
    int i;
    struct mkroom *curr;

    for (curr = lev->rooms, i = 0; i < lev->nroom; curr++, i++)
	if (inside_room(curr, x, y)) return curr;
    return NULL;
}


/* If given a branch, randomly place a special stair or portal. */
void place_branch(struct level *lev,
		  branch *br,		/* branch to place */
		  xchar x,  xchar y)	/* location */
{
	coord	      m;
	d_level	      *dest;
	boolean	      make_stairs;
	struct mkroom *br_room;

	/*
	 * Return immediately if there is no branch to make or we have
	 * already made one.  This routine can be called twice when
	 * a special level is loaded that specifies an SSTAIR location
	 * as a favored spot for a branch.
	 */
	if (!br || made_branch) return;

	if (!x) {	/* find random coordinates for branch */
	    br_room = find_branch_room(lev, &m);
	    x = m.x;
	    y = m.y;
	} else {
	    br_room = pos_to_room(lev, x, y);
	}

	if (on_level(&br->end1, &lev->z)) {
	    /* we're on end1 */
	    make_stairs = br->type != BR_NO_END1;
	    dest = &br->end2;
	} else {
	    /* we're on end2 */
	    make_stairs = br->type != BR_NO_END2;
	    dest = &br->end1;
	}

	if (br->type == BR_PORTAL) {
	    mkportal(lev, x, y, dest->dnum, dest->dlevel);
	} else if (make_stairs) {
	    lev->sstairs.sx = x;
	    lev->sstairs.sy = y;
	    lev->sstairs.up = (char) on_level(&br->end1, &lev->z) ?
					    br->end1_up : !br->end1_up;
	    assign_level(&lev->sstairs.tolev, dest);
	    lev->sstairs_room = br_room;

	    lev->locations[x][y].ladder = lev->sstairs.up ? LA_UP : LA_DOWN;
	    lev->locations[x][y].typ = STAIRS;
	}
	/*
	 * Set made_branch to TRUE even if we didn't make a stairwell (i.e.
	 * make_stairs is false) since there is currently only one branch
	 * per level, if we failed once, we're going to fail again on the
	 * next call.
	 */
	made_branch = TRUE;
}

static boolean bydoor(struct level *lev, xchar x, xchar y)
{
	int typ;

	if (isok(x+1, y)) {
		typ = lev->locations[x+1][y].typ;
		if (IS_DOOR(typ) || typ == SDOOR) return TRUE;
	}
	if (isok(x-1, y)) {
		typ = lev->locations[x-1][y].typ;
		if (IS_DOOR(typ) || typ == SDOOR) return TRUE;
	}
	if (isok(x, y+1)) {
		typ = lev->locations[x][y+1].typ;
		if (IS_DOOR(typ) || typ == SDOOR) return TRUE;
	}
	if (isok(x, y-1)) {
		typ = lev->locations[x][y-1].typ;
		if (IS_DOOR(typ) || typ == SDOOR) return TRUE;
	}
	return FALSE;
}

/* see whether it is allowable to create a door at [x,y] */
int okdoor(struct level *lev, xchar x, xchar y)
{
	boolean near_door = bydoor(lev, x, y);

	return((lev->locations[x][y].typ == HWALL || lev->locations[x][y].typ == VWALL) &&
			lev->doorindex < DOORMAX && !near_door);
}

void dodoor(struct level *lev, int x, int y, struct mkroom *aroom)
{
	if (lev->doorindex >= DOORMAX) {
		impossible("DOORMAX exceeded?");
		return;
	}

	dosdoor(lev, x, y, aroom, rn2(8) ? DOOR : SDOOR);
}

boolean occupied(struct level *lev, xchar x, xchar y)
{
	return((boolean)(t_at(lev, x, y)
		|| IS_FURNITURE(lev->locations[x][y].typ)
		|| is_lava(lev, x, y)
		|| is_pool(lev, x, y)
		|| invocation_pos(&lev->z, x, y)
		));
}

/* make a trap somewhere (in croom if mazeflag = 0 && !tm) */
/* if tm != null, make trap at that location */
void mktrap(struct level *lev, int num, int mazeflag, struct mkroom *croom, coord *tm)
{
	int kind;
	coord m;

	/* no traps in pools */
	if (tm && is_pool(lev, tm->x,tm->y)) return;

	if (num > 0 && num < TRAPNUM) {
	    kind = num;
	} else if (Is_rogue_level(&lev->z)) {
	    switch (rn2(7)) {
		default: kind = BEAR_TRAP; break; /* 0 */
		case 1: kind = ARROW_TRAP; break;
		case 2: kind = DART_TRAP; break;
		case 3: kind = TRAPDOOR; break;
		case 4: kind = PIT; break;
		case 5: kind = SLP_GAS_TRAP; break;
		case 6: kind = RUST_TRAP; break;
	    }
	} else if (In_hell(&lev->z) && !rn2(5)) {
	    /* bias the frequency of fire traps in Gehennom */
	    kind = FIRE_TRAP;
	} else {
	    unsigned lvl = level_difficulty(&lev->z);

	    do {
		kind = rnd(TRAPNUM-1);
		/* reject "too hard" traps */
		switch (kind) {
		    case MAGIC_PORTAL:
                    case VIBRATING_SQUARE:
			kind = NO_TRAP; break;
		    case ROLLING_BOULDER_TRAP:
		    case SLP_GAS_TRAP:
			if (lvl < 2) kind = NO_TRAP; break;
		    case LEVEL_TELEP:
			if (lvl < 5 || lev->flags.noteleport)
			    kind = NO_TRAP; break;
		    case SPIKED_PIT:
			if (lvl < 5) kind = NO_TRAP; break;
		    case LANDMINE:
			if (lvl < 6) kind = NO_TRAP; break;
		    case WEB:
			if (lvl < 7) kind = NO_TRAP; break;
		    case STATUE_TRAP:
		    case POLY_TRAP:
			if (lvl < 8) kind = NO_TRAP; break;
		    case FIRE_TRAP:
			if (!In_hell(&lev->z)) kind = NO_TRAP; break;
		    case TELEP_TRAP:
			if (lev->flags.noteleport) kind = NO_TRAP; break;
		    case HOLE:
			/* make these much less often than other traps */
			if (rn2(7)) kind = NO_TRAP; break;
		}
	    } while (kind == NO_TRAP);
	}

	if ((kind == TRAPDOOR || kind == HOLE) && !can_fall_thru(lev))
		kind = ROCKTRAP;

	if (tm)
	    m = *tm;
	else {
	    int tryct = 0;
	    boolean avoid_boulder = (kind == PIT || kind == SPIKED_PIT ||
				     kind == TRAPDOOR || kind == HOLE);

	    do {
		if (++tryct > 200)
		    return;
		if (mazeflag)
		    mazexy(lev, &m);
		else if (!somexy(lev, croom,&m))
		    return;
	    } while (occupied(lev, m.x, m.y) ||
			(avoid_boulder && sobj_at(BOULDER, lev, m.x, m.y)));
	}

	maketrap(lev, m.x, m.y, kind);
	if (kind == WEB) makemon(&mons[PM_GIANT_SPIDER], lev,
						m.x, m.y, NO_MM_FLAGS);
}


void mkstairs(struct level *lev, xchar x, xchar y, char up, struct mkroom *croom)
{
	if (!x) {
	    impossible("mkstairs:  bogus stair attempt at <%d,%d>", x, y);
	    return;
	}

	/*
	 * We can't make a regular stair off an end of the dungeon.  This
	 * attempt can happen when a special level is placed at an end and
	 * has an up or down stair specified in its description file.
	 */
	if ((dunlev(&lev->z) == 1 && up) ||
			(dunlev(&lev->z) == dunlevs_in_dungeon(&lev->z) && !up))
	    return;

	if (up) {
		lev->upstair.sx = x;
		lev->upstair.sy = y;
		lev->upstairs_room = croom;
	} else {
		lev->dnstair.sx = x;
		lev->dnstair.sy = y;
		lev->dnstairs_room = croom;
	}

	lev->locations[x][y].typ = STAIRS;
	lev->locations[x][y].ladder = up ? LA_UP : LA_DOWN;
}


static void mkfount(struct level *lev, int mazeflag, struct mkroom *croom)
{
	coord m;
	int tryct = 0;

	do {
	    if (++tryct > 200) return;
	    if (mazeflag)
		mazexy(lev, &m);
	    else
		if (!somexy(lev, croom, &m))
		    return;
	} while (occupied(lev, m.x, m.y) || bydoor(lev, m.x, m.y));

	/* Put a fountain at m.x, m.y */
	lev->locations[m.x][m.y].typ = FOUNTAIN;
	/* Is it a "blessed" fountain? (affects drinking from fountain) */
	if (!rn2(7)) lev->locations[m.x][m.y].blessedftn = 1;

	lev->flags.nfountains++;
}


static void mksink(struct level *lev, struct mkroom *croom)
{
	coord m;
	int tryct = 0;

	do {
	    if (++tryct > 200) return;
	    if (!somexy(lev, croom, &m))
		return;
	} while (occupied(lev, m.x, m.y) || bydoor(lev, m.x, m.y));

	/* Put a sink at m.x, m.y */
	lev->locations[m.x][m.y].typ = SINK;

	lev->flags.nsinks++;
}


static void mkaltar(struct level *lev, struct mkroom *croom)
{
	coord m;
	int tryct = 0;
	aligntyp al;

	if (croom->rtype != OROOM) return;

	do {
	    if (++tryct > 200) return;
	    if (!somexy(lev, croom, &m))
		return;
	} while (occupied(lev, m.x, m.y) || bydoor(lev, m.x, m.y));

	/* Put an altar at m.x, m.y */
	lev->locations[m.x][m.y].typ = ALTAR;

	/* -1 - A_CHAOTIC, 0 - A_NEUTRAL, 1 - A_LAWFUL */
	al = rn2((int)A_LAWFUL+2) - 1;
	lev->locations[m.x][m.y].altarmask = Align2amask( al );
}

static void mkgrave(struct level *lev, struct mkroom *croom)
{
	coord m;
	int tryct = 0;
	struct obj *otmp;
	boolean dobell = !rn2(10);


	if (croom->rtype != OROOM) return;

	do {
	    if (++tryct > 200) return;
	    if (!somexy(lev, croom, &m))
		return;
	} while (occupied(lev, m.x, m.y) || bydoor(lev, m.x, m.y));

	/* Put a grave at m.x, m.y */
	make_grave(lev, m.x, m.y, dobell ? "Saved by the bell!" : NULL);

	/* Possibly fill it with objects */
	if (!rn2(3)) mkgold(0L, lev, m.x, m.y);
	for (tryct = rn2(5); tryct; tryct--) {
	    otmp = mkobj(lev, RANDOM_CLASS, TRUE);
	    if (!otmp) return;
	    curse(otmp);
	    otmp->ox = m.x;
	    otmp->oy = m.y;
	    add_to_buried(otmp);
	}

	/* Leave a bell, in case we accidentally buried someone alive */
	if (dobell) mksobj_at(BELL, lev, m.x, m.y, TRUE, FALSE);
	return;
}


/* maze levels have slightly different constraints from normal levels */
#define x_maze_min 2
#define y_maze_min 2
/*
 * Major level transmutation: add a set of stairs (to the Sanctum) after
 * an earthquake that leaves behind a a new topology, centered at inv_pos.
 * Assumes there are no rooms within the invocation area and that inv_pos
 * is not too close to the edge of the map.  Also assume the hero can see,
 * which is guaranteed for normal play due to the fact that sight is needed
 * to read the Book of the Dead.
 */
void mkinvokearea(void)
{
    int dist;
    xchar xmin = inv_pos.x, xmax = inv_pos.x;
    xchar ymin = inv_pos.y, ymax = inv_pos.y;
    xchar i;

    pline("The floor shakes violently under you!");
    pline("The walls around you begin to bend and crumble!");
    win_pause_output(P_MESSAGE);

    mkinvpos(xmin, ymin, 0);		/* middle, before placing stairs */

    for (dist = 1; dist < 7; dist++) {
	xmin--; xmax++;

	/* top and bottom */
	if (dist != 3) { /* the area is wider that it is high */
	    ymin--; ymax++;
	    for (i = xmin+1; i < xmax; i++) {
		mkinvpos(i, ymin, dist);
		mkinvpos(i, ymax, dist);
	    }
	}

	/* left and right */
	for (i = ymin; i <= ymax; i++) {
	    mkinvpos(xmin, i, dist);
	    mkinvpos(xmax, i, dist);
	}

	flush_screen();	/* make sure the new glyphs shows up */
	win_delay_output();
    }

    pline("You are standing at the top of a stairwell leading down!");
    mkstairs(level, u.ux, u.uy, 0, NULL); /* down */
    newsym(u.ux, u.uy);
    vision_full_recalc = 1;	/* everything changed */
}


/* Change level topology.  Boulders in the vicinity are eliminated.
 * Temporarily overrides vision in the name of a nice effect.
 */
static void mkinvpos(xchar x, xchar y, int dist)
{
    struct trap *ttmp;
    struct obj *otmp;
    boolean make_rocks;
    struct rm *loc = &level->locations[x][y];

    /* clip at existing map borders if necessary */
    if (!within_bounded_area(x, y, x_maze_min + 1, y_maze_min + 1,
				   x_maze_max - 1, y_maze_max - 1)) {
	/* only outermost 2 columns and/or rows may be truncated due to edge */
	if (dist < (7 - 2))
	    panic("mkinvpos: <%d,%d> (%d) off map edge!", x, y, dist);
	return;
    }

    /* clear traps */
    if ((ttmp = t_at(level, x,y)) != 0) deltrap(ttmp);

    /* clear boulders; leave some rocks for non-{moat|trap} locations */
    make_rocks = (dist != 1 && dist != 4 && dist != 5) ? TRUE : FALSE;
    while ((otmp = sobj_at(BOULDER, level, x, y)) != 0) {
	if (make_rocks) {
	    fracture_rock(otmp);
	    make_rocks = FALSE;		/* don't bother with more rocks */
	} else {
	    obj_extract_self(otmp);
	    obfree(otmp, NULL);
	}
    }
    unblock_point(x,y);	/* make sure vision knows this location is open */

    /* fake out saved state */
    loc->seenv = 0;
    loc->doormask = 0;
    if (dist < 6) loc->lit = TRUE;
    loc->waslit = TRUE;
    loc->horizontal = FALSE;
    viz_array[y][x] = (dist < 6 ) ?
	(IN_SIGHT|COULD_SEE) : /* short-circuit vision recalc */
	COULD_SEE;

    switch(dist) {
    case 1: /* fire traps */
	if (is_pool(level, x,y)) break;
	loc->typ = ROOM;
	ttmp = maketrap(level, x, y, FIRE_TRAP);
	if (ttmp) ttmp->tseen = TRUE;
	break;
    case 0: /* lit room locations */
    case 2:
    case 3:
    case 6: /* unlit room locations */
	loc->typ = ROOM;
	break;
    case 4: /* pools (aka a wide moat) */
    case 5:
	loc->typ = MOAT;
	/* No kelp! */
	break;
    default:
	impossible("mkinvpos called with dist %d", dist);
	break;
    }

    /* display new value of position; could have a monster/object on it */
    newsym(x,y);
}

/*
 * The portal to Ludios is special.  The entrance can only occur within a
 * vault in the main dungeon at a depth greater than 10.  The Ludios branch
 * structure reflects this by having a bogus "source" dungeon:  the value
 * of n_dgns (thus, Is_branchlev() will never find it).
 *
 * Ludios will remain isolated until the branch is corrected by this function.
 */
static void mk_knox_portal(struct level *lev, xchar x, xchar y)
{
	extern int n_dgns;		/* from dungeon.c */
	d_level *source;
	branch *br;
	schar u_depth;

	br = dungeon_branch("Fort Ludios");
	if (on_level(&knox_level, &br->end1)) {
	    source = &br->end2;
	} else {
	    /* disallow Knox branch on a level with one branch already */
	    if (Is_branchlev(&lev->z))
		return;
	    source = &br->end1;
	}

	/* Already set or 2/3 chance of deferring until a later level. */
	if (source->dnum < n_dgns || (rn2(3) && !wizard)) return;

	if (! (lev->z.dnum == oracle_level.dnum	    /* in main dungeon */
		&& !at_dgn_entrance(&lev->z, "The Quest") /* but not Quest's entry */
		&& (u_depth = depth(&lev->z)) > 10    /* beneath 10 */
		&& u_depth < depth(&medusa_level))) /* and above Medusa */
	    return;

	/* Adjust source to be current level and re-insert branch. */
	*source = lev->z;
	insert_branch(br, TRUE);

	place_branch(lev, br, x, y);
}

/*
 * Places a random engraving from an array of engravings into
 * a random room on the current level.
 */
void place_random_engraving(struct level *lev, const char *const engravings[], const int size)
{
	struct mkroom *some_room;
	xchar sx, sy;
	int tries;
	char const *engraving = engravings[rn2(size)];

	tries = 0;
	do {
	    if (Inhell || lev->nroom <= 1) {
		/* random placement in Gehennom or levels with no rooms */
		sx = rn2(COLNO);
		sy = rn2(ROWNO);
	    } else {
		/* placement inside of rooms, not corridors */
		some_room = &lev->rooms[rn2(lev->nroom - 1)];
		sx = somex(some_room);
		sy = somey(some_room);
	    }
	    tries++;
	} while (occupied(lev, sx, sy) && tries < 100);

	if (tries < 100)
	    make_engr_at(lev, sx, sy, engraving, 0L, MARK);
}

/*
 * Places dungeon level dependent engravings on the current level.
 */
void place_random_engravings(struct level *lev)
{
	switch (depth(&u.uz)) {
	    case 23: place_random_engraving(lev, illuminati_engravings, SIZE(illuminati_engravings)); break;
	    case 42: place_random_engraving(lev, hhgtg_engravings, SIZE(hhgtg_engravings)); break;
	}
}

/*mklev.c*/
