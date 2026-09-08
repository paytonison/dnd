#!/usr/bin/env python3
"""Generate the licensed SRD 5.2.1 magic-item catalog and explicit supported effects.

Requires pypdf and pymupdf. The verified source PDF stays outside distribution.
Red item headings are identified by the PDF's actual typography, then matched to
text extraction. Unknown mechanics remain explicitly cataloged, never executed.
"""
import argparse, hashlib, json, re
from pathlib import Path

URL='https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf'
SHA='8974902d109d6e63672d7c490bde9ccf052410503d9cfa768237154fbc5e3d87'
ABILITIES=['strength','dexterity','constitution','intelligence','wisdom','charisma']
DAMAGE=['acid','cold','fire','force','lightning','necrotic','poison','psychic','radiant','thunder']
def slug(s):return re.sub('[^a-z0-9]+','-',s.lower().replace('’','').replace("'",'')).strip('-')
def source(n):return {'publication':'System Reference Document 5.2.1','page':str(n),'url':URL+'#page='+str(n)}
def text(s):
    s=re.sub(r'(?<=\w)\s*[-\u00ad]\s*\n(?=\w)','',s)
    s=re.sub(r'@@PAGE:\d+@@','',s)
    return re.sub(r'\s+',' ',s).strip()
def fx(op,**values):return dict(op=op,**values)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--pdf',type=Path,required=True);p.add_argument('--cache',type=Path);p.add_argument('--content',type=Path,default=Path('data/packs/srd55-core-v2/content.json'));p.add_argument('--out',type=Path,default=Path('data/packs/srd55-core-v2/magic-items.json'));a=p.parse_args()
    if hashlib.sha256(a.pdf.read_bytes()).hexdigest()!=SHA:raise ValueError('Review the source PDF before changing the verified hash.')
    import pymupdf
    from pypdf import PdfReader
    pdf=pymupdf.open(a.pdf);headings=[]
    for page in range(209,254):
        for block in pdf[page-1].get_text('dict')['blocks']:
            current=[]
            for line in block.get('lines',[]):
                spans=line['spans'];value=''.join(s['text'] for s in spans).strip()
                if spans and all(s['font']=='GillSans-SemiBold' and abs(s['size']-12)<.1 and s['color']==9183776 for s in spans):current.append(value)
                elif current:headings.append((page,' '.join(current)));current=[]
            if current:headings.append((page,' '.join(current)))
    assert len(headings)==258,'All original magic-item headings must be accounted for.'
    cached=json.loads(a.cache.read_text(encoding='utf-8')) if a.cache and a.cache.exists() else None
    if cached and cached.get('sha256')==SHA:pages={int(n):v for n,v in cached['pages'].items()}
    else:
        reader=PdfReader(a.pdf);pages={n:reader.pages[n-1].extract_text() for n in range(209,254)}
    for n,v in pages.items():pages[n]=re.sub(r'^\s*'+str(n)+r'\s*\n','',v.replace('System Reference Document 5.2.1',''))
    raw='\n'.join('@@PAGE:'+str(n)+'@@\n'+pages[n] for n in range(209,254))
    starts=[]
    for page,name in headings:
        pattern=r'^'+r'\s+'.join(re.escape(w) for w in name.split())+r'\s*\n'
        match=re.search(pattern,raw,re.M|re.I)
        if not match:raise ValueError('Cannot locate original heading: '+name)
        starts.append((match.start(),match.end(),page,name))
    starts.sort();base={e['id']:e for e in json.loads(a.content.read_text(encoding='utf-8'))};result=[]
    for i,(start,end,page,name) in enumerate(starts):
        body=raw[end:starts[i+1][0] if i+1<len(starts) else len(raw)];meta=[];offset=0
        body_lines=body.splitlines(keepends=True)
        for line_index,line in enumerate(body_lines):
            offset+=len(line)
            if not line.strip():continue
            meta.append(line.strip());joined=' '.join(meta)
            if any(r in joined for r in ['Common','Uncommon','Rare','Legendary','Artifact','Varies']) and joined.count('(')==joined.count(')') and not joined.endswith((',', 'Very','Requires')):
                following=next((x.strip() for x in body_lines[line_index+1:] if x.strip()),'')
                if not following.startswith('(Requires Attunement'):break
        metadata=' '.join(meta);description=text(body[offset:]);category=metadata.split(' (')[0].split(',')[0].lower().replace(' ','-')
        if category not in ['armor','weapon','wondrous-item','ring','rod','staff','wand','potion','scroll']:raise ValueError((name,metadata))
        short=re.sub(r', \+1, \+2, or \+3$','',name)
        eid='srd55:magic-'+slug(short)+('-bonus' if short=='Weapon' else '')
        if eid in base:raise ValueError('Magic-item identifier collides with existing content: '+eid)
        rarity_match=re.search(r'\b(Common|Uncommon|Very Rare|Rare|Legendary|Artifact|Rarity Varies)\b',metadata)
        rarity=rarity_match[1].lower().replace('rarity ','')
        if ', +1, +2, or +3' in name or 'Rarity Varies' in metadata:rarity='varies'
        prereqs={};clause=re.search(r'Requires Attunement by (.+)\)',metadata)
        if clause:
            prereqs['description']=clause[1];classes=[c.lower() for c in ['Barbarian','Bard','Cleric','Druid','Fighter','Monk','Paladin','Ranger','Rogue','Sorcerer','Warlock','Wizard'] if re.search(r'\b'+c+r'\b',clause[1])]
            if classes:prereqs['anyClass']=classes
            if 'Spellcaster' in clause[1]:prereqs['spellcaster']=True
            if 'Dwarf' in clause[1]:prereqs['dwarfOrBeltOfDwarvenkind']=True
        e={'id':eid,'kind':'magic-item','name':name,'source':source(page),'category':category,'rarity':rarity,'metadata':metadata,'description':description,'requiresAttunement':'Requires Attunement' in metadata,'attunementPrerequisites':prereqs,'effects':[],'actions':[],'wearSlot':'carried','coverage':{'status':'cataloged','passive':'unimplemented','actions':'unimplemented','remaining':['Full source text is available; mechanics have not been implemented for this record.']}}
        if category=='weapon':
            scope=re.search(r'^Weapon \(([^)]+)\)',metadata);scope=scope[1] if scope else ''
            if 'Ammunition' in scope:e['baseOptions']=[x for x in base if x in ['srd55:arrows-20','srd55:bolts-20','srd55:sling-bullets-20']]
            else:
                allowed=[]
                for bid,item in base.items():
                    if item['kind']!='weapon':continue
                    properties=item.get('properties','')
                    valid=('Any' in scope or item['name'] in scope)
                    if 'Sword' in scope:valid=any(t in item['name'] for t in ['sword','Scimitar','Rapier'])
                    if 'Axe' in scope:valid=any(t in item['name'] for t in ['axe','Halberd'])
                    if 'Slashing' in scope:valid=valid and item['damageType']=='Slashing'
                    if 'Melee' in scope:valid=valid and not item['ranged']
                    if 'Ranged' in scope:valid=valid and item['ranged']
                    if valid:allowed.append(bid)
                e['baseOptions']=allowed
            e['wearSlot']='main-hand'
        elif category=='armor':
            scope=re.search(r'^Armor \(([^)]+)\)',metadata);scope=scope[1] if scope else ''
            if scope=='Shield':e['baseProfile']='srd55:shield-equipment';e['wearSlot']='off-hand'
            else:
                e['baseOptions']=[bid for bid,item in base.items() if item['kind']=='armor' and (item['name'] in scope or ('Any' in scope and (item['category'].title() in scope or 'Light, Medium, or Heavy' in scope))) and not ('Except Hide' in scope and bid=='srd55:hide-armor')];e['wearSlot']='armor'
        elif category=='staff':e['baseProfile']='srd55:quarterstaff';e['wearSlot']='main-hand'
        elif category in ['wand','rod']:e['wearSlot']='main-hand'
        elif category=='ring':e['wearSlot']='ring'
        elif category=='wondrous-item':
            for term,slot in [('Boots','boots'),('Slippers','boots'),('Gloves','gloves'),('Gauntlets','gloves'),('Bracers','bracers'),('Cloak','cloak'),('Cape','cloak'),('Robe','worn'),('Hat','head'),('Helm','head'),('Headband','head'),('Circlet','head'),('Goggles','eyes'),('Amulet','neck'),('Necklace','neck'),('Periapt','neck'),('Medallion','neck'),('Belt','belt'),('Ioun Stone','orbit')]:
                if name.startswith(term):e['wearSlot']=slot;break
        charges=re.search(r'\bhas (\d+) charges',description)
        if charges:
            e['charges']={'maximum':int(charges[1]),'initial':int(charges[1]),'unit':'charges'}
            recharge=re.search(r'regains (\d+)d(\d+)(?: \+ (\d+))? expended charges (?:daily )?at dawn',description)
            if recharge:e['charges']['recharge']={'event':'dawn','dice':int(recharge[1]),'sides':int(recharge[2]),'bonus':int(recharge[3] or 0)}
            if 'last charge' in description and ('roll a d20' in description or 'roll 1d20' in description):e['charges']['depletion']={'sides':20,'destroyOn':1}
        if 'Curse.' in description:e['curse']={'persistent':True,'blocksDisposition':'unwilling to part' in description,'description':description.split('Curse.',1)[1],'effects':[]}
        result.append(e)
    items={e['id']:e for e in result}
    def get(name):return items['srd55:magic-'+slug(name)+('-bonus' if name=='Weapon' else '')]
    def supported(name,effects=(),actions=(),remaining=()):
        e=get(name);e['effects']=list(effects);e['actions']=list(actions);e['coverage']={'status':'partial' if remaining else 'implemented','passive':'implemented','actions':'partial' if remaining else 'implemented','remaining':list(remaining)};return e
    # The PDF places this full-width table below two intervening armor entries.
    # Restore its actual owner rather than leaving it in Armor of Resistance.
    misplaced=get('Armor of Resistance');caption='Apparatus of the Crab Levers'
    if caption in misplaced['description']:
        before,table=misplaced['description'].split(caption,1)
        misplaced['description']=before.rstrip();get('Apparatus of the Crab')['description']+=' '+caption+table
    # Core numeric variants retain original table identities and require an explicit variant.
    for name,category in [('Armor','armor'),('Weapon','weapon'),('Ammunition','ammunition'),('Shield','shield'),('Wand of the War Mage','wand')]:
        e=get(name);e['variants']=[]
        rarities=['rare','very rare','legendary'] if category=='armor' else ['uncommon','rare','very rare']
        for n,r in enumerate(rarities,1):
            effects=[fx('ac-bonus',value=n)] if category in ['armor','shield'] else [fx('spell-attack-bonus',value=n),fx('ignore-half-cover',scope='spell-attacks')] if category=='wand' else [fx('weapon-attack-bonus',value=n),fx('weapon-damage-bonus',value=n)]
            e['variants'].append({'id':'+'+str(n),'name':'+'+str(n),'rarity':r,'effects':effects})
        e['coverage']={'status':'partial' if category=='ammunition' else 'implemented','passive':'implemented','actions':'partial' if category=='ammunition' else 'implemented','remaining':['Magic ammunition loses its magic after hitting; explicit consumption is still required.'] if category=='ammunition' else []}
    for name,ability in [('Amulet of Health','constitution'),('Gauntlets of Ogre Power','strength'),('Headband of Intellect','intelligence')]:supported(name,[fx('ability-minimum',ability=ability,value=19)])
    e=supported('Belt of Giant Strength');e['variants']=[{'id':n,'name':n.title()+' Giant','rarity':r,'effects':[fx('ability-minimum',ability='strength',value=v)]} for n,v,r in [('hill',21,'rare'),('stone',23,'very rare'),('frost',23,'very rare'),('fire',25,'very rare'),('cloud',27,'legendary'),('storm',29,'legendary')]]
    supported('Belt of Dwarvenkind',[fx('ability-bonus',ability='constitution',value=2,maxScore=20),fx('language-grant',languageId='srd55:dwarvish'),fx('advantage',test='persuasion',condition='interacting-with-dwarves-or-duergar'),fx('darkvision-minimum',value=60,condition='not-dwarf'),fx('resistance',types=['poison'],condition='not-dwarf'),fx('advantage',test='saves-vs-poisoned',condition='not-dwarf')],remaining=['The cosmetic dawn beard-growth roll is referenced rather than automatically rolled.'])
    e=supported('Ioun Stone',remaining=['Absorption counters, Reserve spell storage, and hourly Regeneration transactions are not yet implemented.'])
    e['maximumEquipped']=3;e['variants']=[]
    for variant,rank,ability in [('agility','very rare','dexterity'),('fortitude','very rare','constitution'),('insight','very rare','wisdom'),('intellect','very rare','intelligence'),('leadership','very rare','charisma'),('strength','very rare','strength')]:e['variants'].append({'id':variant,'name':variant.title(),'rarity':rank,'effects':[fx('ability-bonus',ability=ability,value=2,maxScore=20)],'coverage':{'status':'implemented','passive':'implemented','actions':'implemented','remaining':[]}})
    for variant,rank,effects,status in [('awareness','rare',[fx('advantage',test='initiative'),fx('advantage',test='perception')],'implemented'),('mastery','legendary',[fx('proficiency-bonus',value=1)],'implemented'),('protection','rare',[fx('ac-bonus',value=1)],'implemented'),('sustenance','rare',[fx('no-food-or-water-required')],'implemented'),('regeneration','legendary',[fx('regeneration',value=15,intervalHours=1,requiresPositiveHp=True)],'partial'),('absorption','very rare',[],'cataloged'),('greater-absorption','legendary',[],'cataloged'),('reserve','rare',[],'cataloged')]:e['variants'].append({'id':variant,'name':variant.title(),'rarity':rank,'effects':effects,'coverage':{'status':status,'passive':'implemented' if effects else 'none','actions':'unimplemented' if status!='implemented' else 'implemented','remaining':[] if status=='implemented' else ['The full source property is retained; this variant action is not yet executed.']}})
    for name in ['Cloak of Protection','Ring of Protection']:supported(name,[fx('ac-bonus',value=1),fx('save-bonus',value=1)])
    supported('Bracers of Archery',[fx('weapon-training',weaponProfiles=['srd55:longbow','srd55:shortbow']),fx('weapon-damage-bonus',value=2,weaponProfiles=['srd55:longbow','srd55:shortbow'])])
    supported('Bracers of Defense',[fx('ac-bonus',value=2,condition='no-armor-or-shield')])
    supported('Goggles of Night',[fx('darkvision-bonus',value=60)])
    supported('Stone of Good Luck (Luckstone)',[fx('ability-check-bonus',value=1),fx('save-bonus',value=1)])
    supported('Adamantine Armor',[fx('critical-hit-immunity',scope='incoming')])
    supported('Mithral Armor',[fx('armor-strength-requirement-ignored'),fx('armor-stealth-disadvantage-ignored')])
    supported('Boots of Striding and Springing',[fx('speed-minimum',mode='walk',value=30),fx('armor-speed-penalty-ignored'),fx('encumbrance-speed-penalty-ignored'),fx('jump',feet=30,movementCost=10,maximumPerTurn=1)])
    supported('Boots of Elvenkind',[fx('advantage',test='stealth',condition='moving'),fx('silent-footsteps')])
    supported('Cloak of Elvenkind',[fx('advantage',test='stealth',condition='hood-up'),fx('disadvantage-to-observer',test='perception',condition='hood-up')],remaining=['Hood position and situational observer checks are displayed; external encounter resolution remains manual.'])
    supported('Cloak of Displacement',[fx('disadvantage-to-attacker',condition='displacement-active')],remaining=['Taking damage suppresses the illusion until the next turn; this timing is not automated.'])
    supported('Ring of Free Action',[fx('ignore-difficult-terrain'),fx('immunity',types=['paralyzed','restrained'],condition='magical'),fx('speed-reduction-immunity',condition='magical')])
    supported('Ring of Swimming',[fx('speed-minimum',mode='swim',value=40)])
    supported('Ring of Water Walking',[fx('water-walking')])
    supported('Ring of Feather Falling',[fx('feather-fall',condition='falling')])
    supported('Necklace of Adaptation',[fx('advantage',test='saves-vs-poisoned'),fx('breathe-any-environment')])
    e=supported('Periapt of Health',[fx('advantage',test='saves-vs-poisoned')],[{'id':'heal','name':'Regain 2d4 + 2 hit points','kind':'heal','chargeCost':1,'roll':{'dice':2,'sides':4,'bonus':2}}]);e['charges']={'maximum':1,'initial':1,'unit':'uses','recharge':{'event':'dawn','fixed':1}}
    supported('Periapt of Proof against Poison',[fx('immunity',types=['poison','poisoned'])])
    for name in ['Ring of Resistance','Armor of Resistance']:
        e=supported(name);e['variants']=[{'id':t,'name':t.title(),'rarity':e['rarity'],'effects':[fx('resistance',types=[t])]} for t in DAMAGE]
    e=supported('Armor of Vulnerability',remaining=['Persistent curse and damage-type changes are tracked; damage application remains external.']);e['variants']=[{'id':t,'name':t.title(),'rarity':'rare','effects':[fx('resistance',types=[t])],'curseEffects':[fx('vulnerability',types=[x for x in ['bludgeoning','piercing','slashing'] if x!=t])]} for t in ['bludgeoning','piercing','slashing']]
    e=supported('Berserker Axe',[fx('weapon-attack-bonus',value=1),fx('weapon-damage-bonus',value=1),fx('hp-per-level',value=1,condition='attuned')],remaining=['Berserk trigger and forced attacks require encounter adjudication.']);e['curse']['effects']=[fx('attack-disadvantage',scope='other-weapons')]
    supported('Robe of the Archmagi',[fx('armor-formula',base=15,abilities=['dexterity'],condition='no-armor'),fx('advantage',test='saves-vs-magic'),fx('spell-attack-bonus',value=2),fx('spell-save-bonus',value=2)])
    for name,ability in [('Manual of Bodily Health','constitution'),('Manual of Gainful Exercise','strength'),('Manual of Quickness of Action','dexterity'),('Tome of Clear Thought','intelligence'),('Tome of Leadership and Influence','charisma'),('Tome of Understanding','wisdom')]:
        supported(name,actions=[{'id':'study','name':'Complete 48-hour study','kind':'permanent-ability','ability':ability,'value':2,'maxScore':30,'hours':48,'maximumDays':6,'dormantYears':100}])
    e=supported('Potions of Healing');e['charges']={'maximum':1,'initial':1,'unit':'consumable','consumedWhenEmpty':True}
    e['variants']=[{'id':kind,'name':name,'rarity':rank,'effects':[],'actions':[{'id':'drink','name':'Drink or administer '+name,'kind':'heal','chargeCost':1,'administerable':True,'roll':{'dice':dice,'sides':4,'bonus':bonus}}]} for kind,name,rank,dice,bonus in [('healing','Healing','common',2,2),('greater','Greater Healing','uncommon',4,4),('superior','Superior Healing','rare',8,8),('supreme','Supreme Healing','very rare',10,20)]]
    e=supported('Spell Scroll',remaining=['Owned scroll copying is implemented through the lifecycle route; casting a scroll remains a separate source-reference action.']);e['spellSelection']=True;e['charges']={'maximum':1,'initial':1,'unit':'consumable','consumedWhenEmpty':True}
    e['variants']=[{'id':'level-'+str(level),'name':'Cantrip' if level==0 else 'Level '+str(level),'rarity':rank,'scrollLevel':level,'saveDc':dc,'attackBonus':attack,'effects':[]} for level,rank,dc,attack in [(0,'common',13,5),(1,'common',13,5),(2,'uncommon',13,5),(3,'uncommon',15,7),(4,'rare',15,7),(5,'rare',17,9),(6,'very rare',17,9),(7,'very rare',18,10),(8,'very rare',18,10),(9,'legendary',19,11)]]
    # Supported charged spell profiles; charges are transactions, never recalculated rolls.
    def spell_action(spell,cost,level,dc=None,**extra):
        action={'id':'cast-'+slug(spell),'name':'Cast '+spell,'kind':'cast-spell','spellId':'srd55:'+slug(spell),'chargeCost':cost,'castLevel':level};action.update(extra)
        if dc is not None:action['saveDc']=dc
        return action
    for name,spell,dc,level in [('Wand of Fireballs','Fireball',15,3),('Wand of Lightning Bolts','Lightning Bolt',15,3),('Wand of Web','Web',15,2),('Wand of Polymorph','Polymorph',15,4)]:supported(name,actions=[spell_action(spell,1,level,dc,upcastWithCharges=spell in ['Fireball','Lightning Bolt'])])
    supported('Wand of Magic Missiles',actions=[spell_action('Magic Missile',1,1,upcastWithCharges=True)])
    e=supported('Ring of Three Wishes',actions=[spell_action('Wish',1,9)],remaining=['Wish consequences and target outcomes require the spell rules and GM.']);e['charges']={'maximum':3,'initial':3,'unit':'charges','mundaneWhenEmpty':True}
    supported('Boots of Levitation',actions=[spell_action('Levitate',0,2,selfOnly=True,usesActorCastingAbility=True)])
    supported('Helm of Teleportation',actions=[spell_action('Teleport',1,7,usesActorCastingAbility=True)],remaining=['Teleport destination accuracy and mishap outcomes require accepted encounter adjudication.'])
    e=supported('Cape of the Mountebank',actions=[spell_action('Dimension Door',1,4)],remaining=['Lightly obscuring smoke at the departure space through the end of the next turn is a referenced encounter effect.']);e['charges']={'maximum':1,'initial':1,'unit':'uses','recharge':{'event':'dawn','fixed':1}}
    for name,spells in [('Staff of Fire',[('Burning Hands',1,1),('Fireball',3,3),('Wall of Fire',4,4)]),('Staff of Frost',[('Cone of Cold',5,5),('Fog Cloud',1,1),('Ice Storm',4,4),('Wall of Ice',4,6)]),('Staff of Healing',[('Cure Wounds',1,1),('Lesser Restoration',2,2),('Mass Cure Wounds',5,5)])]:
        effects=[fx('resistance',types=['fire' if name=='Staff of Fire' else 'cold'])] if name!='Staff of Healing' else []
        supported(name,effects,[spell_action(s,c,l,usesActorCastingAbility=True,**({'upcastWithCharges':True,'maximumCharges':4} if s=='Cure Wounds' else {})) for s,c,l in spells])
    supported('Staff of Striking',[fx('weapon-attack-bonus',value=3),fx('weapon-damage-bonus',value=3)],remaining=['Its optional charge-powered extra damage is not yet an executed action.'])
    # These static character effects are implemented while more involved actions stay marked partial.
    supported('Staff of Power',[fx('weapon-attack-bonus',value=2),fx('weapon-damage-bonus',value=2),fx('ac-bonus',value=2),fx('save-bonus',value=2),fx('spell-attack-bonus',value=2)],remaining=['Spell actions, Power Strike, and Retributive Strike remain source-reference mechanics.'])
    supported('Staff of the Magi',[fx('weapon-attack-bonus',value=2),fx('weapon-damage-bonus',value=2),fx('spell-attack-bonus',value=2),fx('advantage',test='saves-vs-spells')],remaining=['Spell Absorption, spell actions, and Retributive Strike remain source-reference mechanics.'])
    supported('Amulet of Proof against Detection and Location',[fx('divination-targeting-immunity'),fx('scrying-immunity')])
    # Preserve exact rule-based current identity, including variably rare templates.
    for e in result:
        if e.get('variants'):
            for v in e['variants']:v.setdefault('effects',[])
        if e.get('baseOptions') and len(e['baseOptions'])==1:e['baseProfile']=e.pop('baseOptions')[0]
        if e.get('charges') and not e['actions'] and not any(v.get('actions') for v in e.get('variants',[])):e['coverage']['actions']='unimplemented'
        if e['id']=='srd55:magic-ammunition':e['coverage'].update(passive='partial',remaining=['Magic ammunition selection and per-shot consumption remain source-reference mechanics; the numeric bonus requires a selected compatible ranged ammunition context.'])
        for action in e['actions']+[a for v in e.get('variants',[]) for a in v.get('actions',[])]:
            if action.get('spellId'):
                if action['spellId'] not in base:raise ValueError('Missing spell '+action['spellId'])
                if action.get('castLevel',0)<base[action['spellId']]['level']:raise ValueError('Item casts below spell level: '+e['id']+' '+action['spellId'])
            if action.get('chargeCost',0)>0 and 'charges' not in e:raise ValueError('Charged action has no charge capacity: '+e['id'])
        e['descriptionCoverage']='complete-source-text'
    assert len({e['id'] for e in result})==258
    a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
    from collections import Counter
    print('258 original item headings:',dict(Counter(e['coverage']['status'] for e in result)))
    print('Attunement',sum(e['requiresAttunement'] for e in result),'charge records',sum('charges' in e for e in result))

if __name__=='__main__':main()
