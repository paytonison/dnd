#!/usr/bin/env python3
"""Rebuild the SRD 5.2.1 v2 pack from the official PDF and the preserved v1 pack.

Requires pypdf. The PDF remains external to the repository/distribution. Tables are
transcribed from visually checked source pages; text extraction supplies licensed
feature/spell descriptions, not executable Python or arbitrary rule expressions.
"""
from __future__ import annotations
import argparse, copy, hashlib, json, re
from pathlib import Path

URL='https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf'
SOURCE='System Reference Document 5.2.1'
VERIFIED_SHA256='8974902d109d6e63672d7c490bde9ccf052410503d9cfa768237154fbc5e3d87'
ABILITIES=['strength','dexterity','constitution','intelligence','wisdom','charisma']
NAMES=['Barbarian','Bard','Cleric','Druid','Fighter','Monk','Paladin','Ranger','Rogue','Sorcerer','Warlock','Wizard']
PAGES=[28,31,36,41,47,49,53,57,61,64,70,77]
SUBCLASSES=['Path of the Berserker','College of Lore','Life Domain','Circle of the Land','Champion','Warrior of the Open Hand','Oath of Devotion','Hunter','Thief','Draconic Sorcery','Fiend Patron','Evoker']
SUBPAGES=[30,35,40,46,49,52,56,61,64,69,76,82]
FULL_SLOTS=[[2],[3],[4,2],[4,3],[4,3,2],[4,3,3],[4,3,3,1],[4,3,3,2],[4,3,3,3,1],[4,3,3,3,2],[4,3,3,3,2,1],[4,3,3,3,2,1],[4,3,3,3,2,1,1],[4,3,3,3,2,1,1],[4,3,3,3,2,1,1,1],[4,3,3,3,2,1,1,1],[4,3,3,3,2,1,1,1,1],[4,3,3,3,3,1,1,1,1],[4,3,3,3,3,2,1,1,1],[4,3,3,3,3,2,2,1,1]]
FULL_SLOTS=[row+[0]*(9-len(row)) for row in FULL_SLOTS]
FULL_PREP=[4,5,6,7,9,10,11,12,14,15,16,16,17,17,18,18,19,20,21,22]
HALF_PREP=[2,3,4,5,6,6,7,7,9,9,10,10,11,11,12,12,14,14,15,15]
XP=[0,300,900,2700,6500,14000,23000,34000,48000,64000,85000,100000,120000,140000,165000,195000,225000,265000,305000,355000]


def slug(name):
    return re.sub(r'[^a-z0-9]+','-',name.casefold().replace('’','').replace("'",'')).strip('-')
def ident(name):return 'srd55:'+slug(name)
def source(page):return {'publication':SOURCE,'page':str(page),'url':URL+'#page='+str(page).split('–')[0]}
def clean(text):
    text=re.sub(r'(?<=\w)\s*[-\u00ad]\s*\n(?=\w)','',text)
    text=re.sub(r'\s*@@PAGE:\d+@@\s*','\n',text)
    return re.sub(r'[ \t]+',' ',text).strip()
def prose(text):return re.sub(r'\s+',' ',clean(text)).strip()
def pad(levels):return [next(v for l,v in reversed(levels) if n>=l) for n in range(1,21)]
def skill_ids(names):return [ident(n) for n in names.split('|') if n]
def kit(gold,*items,choices=None):
    result={'gold':gold,'items':[ident(x) for x in items]}
    if choices:result['choices']=choices
    return result

# Verified core traits and class-entry packages. Weapon qualifiers are explicit.
CORE={
'barbarian':dict(hitDie=12,saves=['strength','constitution'],skillCount=2,skills='Animal Handling|Athletics|Intimidation|Nature|Perception|Survival',armor=['light','medium','shield'],weapons=['simple','martial'],primary=['strength'],mcArmor=['shield'],mcWeapons=['martial'],gold=75,kit=kit(15,'Greataxe',*(['Handaxe']*4),'Explorers Pack')),
'bard':dict(hitDie=8,saves=['dexterity','charisma'],skillCount=3,skills='*',armor=['light'],weapons=['simple'],primary=['charisma'],mcArmor=['light'],mcSkills=1,gold=90,kit=kit(19,'Leather Armor','Dagger','Dagger','Entertainers Pack',choices=[{'id':'instrument','kind':'tool','category':'musical-instrument','count':1}]),toolChoices=[{'kind':'tool','category':'musical-instrument','count':3}],mcToolChoices=[{'kind':'tool','category':'musical-instrument','count':1}]),
'cleric':dict(hitDie=8,saves=['wisdom','charisma'],skillCount=2,skills='History|Insight|Medicine|Persuasion|Religion',armor=['light','medium','shield'],weapons=['simple'],primary=['wisdom'],mcArmor=['light','medium','shield'],gold=110,kit=kit(7,'Chain Shirt','Shield Equipment','Mace','Holy Symbol','Priests Pack')),
'druid':dict(hitDie=8,saves=['intelligence','wisdom'],skillCount=2,skills='Animal Handling|Arcana|Insight|Medicine|Nature|Perception|Religion|Survival',armor=['light','shield'],weapons=['simple'],primary=['wisdom'],mcArmor=['light','shield'],tools=['srd55:herbalism-kit'],gold=50,kit=kit(9,'Leather Armor','Shield Equipment','Sickle','Druidic Focus Staff','Explorers Pack','Herbalism Kit')),
'fighter':dict(hitDie=10,saves=['strength','constitution'],skillCount=2,skills='Acrobatics|Animal Handling|Athletics|History|Insight|Intimidation|Persuasion|Perception|Survival',armor=['light','medium','heavy','shield'],weapons=['simple','martial'],primary=['strength','dexterity'],primaryMode='any',mcArmor=['light','medium','shield'],mcWeapons=['martial']),
'monk':dict(hitDie=8,saves=['strength','dexterity'],skillCount=2,skills='Acrobatics|Athletics|History|Insight|Religion|Stealth',armor=[],weapons=['simple','martial-light'],primary=['dexterity','wisdom'],gold=50,kit=kit(11,'Spear',*(['Dagger']*5),'Explorers Pack',choices=[{'id':'tool','kind':'tool','categories':['artisan','musical-instrument'],'count':1,'matchesProficiency':True}]),toolChoices=[{'kind':'tool','categories':['artisan','musical-instrument'],'count':1}]),
'paladin':dict(hitDie=10,saves=['wisdom','charisma'],skillCount=2,skills='Athletics|Insight|Intimidation|Medicine|Persuasion|Religion',armor=['light','medium','heavy','shield'],weapons=['simple','martial'],primary=['strength','charisma'],mcArmor=['light','medium','shield'],mcWeapons=['martial'],gold=150,kit=kit(9,'Chain Mail','Shield Equipment','Longsword',*(['Javelin']*6),'Holy Symbol','Priests Pack')),
'ranger':dict(hitDie=10,saves=['strength','dexterity'],skillCount=3,skills='Animal Handling|Athletics|Insight|Investigation|Nature|Perception|Stealth|Survival',armor=['light','medium','shield'],weapons=['simple','martial'],primary=['dexterity','wisdom'],mcArmor=['light','medium','shield'],mcWeapons=['martial'],mcSkills=1,gold=150,kit=kit(7,'Studded Leather Armor','Scimitar','Shortsword','Longbow','Arrows 20','Quiver','Druidic Focus Mistletoe','Explorers Pack')),
'rogue':dict(hitDie=8,saves=['dexterity','intelligence'],skillCount=4,skills='Acrobatics|Athletics|Deception|Insight|Intimidation|Investigation|Perception|Persuasion|Sleight of Hand|Stealth',armor=['light'],weapons=['simple','martial-finesse-or-light'],primary=['dexterity'],mcArmor=['light'],mcSkills=1,tools=['srd55:thieves-tools'],mcTools=['srd55:thieves-tools'],gold=100,kit=kit(8,'Leather Armor','Dagger','Dagger','Shortsword','Shortbow','Arrows 20','Quiver','Thieves Tools','Burglars Pack')),
'sorcerer':dict(hitDie=6,saves=['constitution','charisma'],skillCount=2,skills='Arcana|Deception|Insight|Intimidation|Persuasion|Religion',armor=[],weapons=['simple'],primary=['charisma'],gold=50,kit=kit(28,'Spear','Dagger','Dagger','Arcane Focus Crystal','Dungeoneers Pack')),
'warlock':dict(hitDie=8,saves=['wisdom','charisma'],skillCount=2,skills='Arcana|Deception|History|Intimidation|Investigation|Nature|Religion',armor=['light'],weapons=['simple'],primary=['charisma'],mcArmor=['light'],gold=100,kit=kit(15,'Leather Armor','Sickle','Dagger','Dagger','Arcane Focus Orb','Book Occult Lore','Scholars Pack')),
'wizard':dict(hitDie=6,saves=['intelligence','wisdom'],skillCount=2,skills='Arcana|History|Insight|Investigation|Medicine|Nature|Religion',armor=[],weapons=['simple'],primary=['intelligence']),
}


def casting_for(name):
    kind='full' if name in ['bard','cleric','druid','sorcerer','wizard'] else 'half' if name in ['paladin','ranger'] else 'pact' if name=='warlock' else 'none'
    ability={'bard':'charisma','cleric':'wisdom','druid':'wisdom','sorcerer':'charisma','wizard':'intelligence','paladin':'charisma','ranger':'wisdom','warlock':'charisma'}.get(name,'')
    base={'bard':2,'cleric':3,'druid':2,'sorcerer':4,'wizard':3,'warlock':2}.get(name,0)
    cantrips=pad([(1,base),(4,base+1),(10,base+2)]) if base else [0]*20
    prep=FULL_PREP if kind=='full' else HALF_PREP if kind=='half' else [2,3,4,5,6,7,8,9,10,10,11,11,12,12,13,13,14,14,15,15] if kind=='pact' else [0]*20
    if name=='sorcerer':prep=[2,4]+FULL_PREP[2:]
    if name=='wizard':prep=[4,5,6,7,9,10,11,12,14,15,16,16,17,18,19,21,22,23,24,25]
    slots=copy.deepcopy(FULL_SLOTS) if kind=='full' else [FULL_SLOTS[(n+1)//2-1][:5]+[0]*4 for n in range(1,21)] if kind=='half' else [[0]*9 for _ in range(20)]
    if kind=='pact':
        for n in range(1,21):slots[n-1][min(5,(n+1)//2)-1]=1 if n==1 else 2 if n<11 else 3 if n<17 else 4
    return {'ability':ability,'list':name if kind!='none' else '', 'kind':kind,'cantrips':cantrips,'prepared':prep,'slots':slots,'pactSlots':pad([(1,1),(2,2),(11,3),(17,4)]) if kind=='pact' else [0]*20,'pactSlotLevel':pad([(1,1),(3,2),(5,3),(7,4),(9,5)]) if kind=='pact' else [0]*20,'learnMode':'book' if name=='wizard' else 'known' if name in ['bard','sorcerer','warlock'] else 'prepared', 'preparationChange':'class-level-one' if name in ['bard','sorcerer','warlock'] else 'long-rest-one' if name in ['paladin','ranger'] else 'long-rest-any','cantripChange':'long-rest-one' if name in ['cleric','wizard'] else 'class-level-one'}


def progression_for(name):
    p={}
    if name=='barbarian':p={'rages':pad([(1,2),(3,3),(6,4),(12,5),(17,6)]),'rageDamage':pad([(1,2),(9,3),(16,4)]),'weaponMastery':pad([(1,2),(4,3),(10,4)])}
    if name=='bard':p={'bardicDie':pad([(1,'1d6'),(5,'1d8'),(10,'1d10'),(15,'1d12')])}
    if name=='cleric':p={'channelDivinity':pad([(1,0),(2,2),(6,3),(18,4)])}
    if name=='druid':p={'wildShape':pad([(1,0),(2,2),(6,3),(17,4)]),'knownForms':pad([(1,0),(2,4),(4,6),(8,8)]),'maxFormCr':pad([(1,0),(2,.25),(4,.5),(8,1)])}
    if name=='fighter':p={'secondWind':pad([(1,2),(4,3),(10,4)]),'weaponMastery':pad([(1,3),(4,4),(10,5),(16,6)]),'actionSurge':pad([(1,0),(2,1),(17,2)]),'indomitable':pad([(1,0),(9,1),(13,2),(17,3)]),'attacks':pad([(1,1),(5,2),(11,3),(20,4)])}
    if name=='monk':p={'focusPoints':[0]+list(range(2,21)),'martialArtsDie':pad([(1,'1d6'),(5,'1d8'),(11,'1d10'),(17,'1d12')]),'unarmoredMovement':pad([(1,0),(2,10),(6,15),(10,20),(14,25),(18,30)])}
    if name=='paladin':p={'channelDivinity':pad([(1,0),(3,2),(11,3)]),'weaponMastery':[2]*20,'layOnHands':[n*5 for n in range(1,21)]}
    if name=='ranger':p={'favoredEnemy':pad([(1,2),(5,3),(9,4),(13,5),(17,6)]),'weaponMastery':[2]*20}
    if name=='rogue':p={'sneakAttack':[str((n+1)//2)+'d6' for n in range(1,21)],'weaponMastery':[2]*20}
    if name=='sorcerer':p={'sorceryPoints':[0]+list(range(2,21)),'metamagicCount':pad([(1,0),(2,2),(10,4),(17,6)]),'innateSorcery':[2]*20}
    if name=='warlock':p={'invocations':[1,3,3,3,5,5,6,6,7,7,7,8,8,8,9,9,9,10,10,10],'pactSlotLevel':pad([(1,1),(3,2),(5,3),(7,4),(9,5)]),'pactSlotCount':pad([(1,1),(2,2),(11,3),(17,4)])}
    return p


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pdf',type=Path,required=True)
    parser.add_argument('--cache',type=Path,help='Optional extraction cache outside the distributed pack')
    parser.add_argument('--legacy',type=Path,default=Path('data/packs/srd55-core/content.json'))
    parser.add_argument('--out',type=Path,default=Path('data/packs/srd55-core-v2'))
    args=parser.parse_args()
    from pypdf import PdfReader
    reader=PdfReader(args.pdf)
    assert len(reader.pages)==364,'Expected the English 364-page SRD 5.2.1.'
    digest=hashlib.sha256(args.pdf.read_bytes()).hexdigest()
    if digest!=VERIFIED_SHA256:raise ValueError('Source PDF differs from the visually verified English SRD 5.2.1; review it before updating VERIFIED_SHA256.')
    cached=json.loads(args.cache.read_text(encoding='utf-8')) if args.cache and args.cache.exists() else None
    if cached and cached.get('sha256')==digest:pages={int(n):t for n,t in cached['pages'].items()}
    else:
        pages={i+1:p.extract_text() for i,p in enumerate(reader.pages)}
        if args.cache:args.cache.write_text(json.dumps({'sha256':digest,'pages':pages}),encoding='utf-8')
    assert 'System Reference Document 5.2.1' in pages[1]
    for n,t in pages.items():
        t=t.replace('System Reference Document 5.2.1','')
        t=re.sub(r'^\s*'+str(n)+r'\s*\n','',t)
        pages[n]=t
    raw='\n'.join('@@PAGE:'+str(n)+'@@\n'+pages[n] for n in range(1,365))
    def page_at(pos):return int(re.findall(r'@@PAGE:(\d+)@@',raw[:pos])[-1])
    def section(a,b):return '\n'.join('@@PAGE:'+str(n)+'@@\n'+pages[n] for n in range(a,b+1))
    entries={e['id']:e for e in json.loads(args.legacy.read_text(encoding='utf-8')) if e['kind'] not in ['class','subclass','spell','feat']}
    def add(kind,name,page,**kw):
        e=dict(id=ident(name),kind=kind,name=name,source=source(page));e.update(kw)
        prior=entries.get(e['id'])
        if prior and prior['kind']!=kind:raise ValueError('Cross-kind identifier collision: '+e['id']+' '+prior['kind']+' / '+kind)
        entries[e['id']]=e;return e
    def gear(name,cp,page=95,**kw):return add('gear',name,page,costCp=cp,**kw)
    for name,cp in [('Explorers Pack',1000),('Entertainers Pack',4000),('Priests Pack',3300),('Burglars Pack',1600),('Book Occult Lore',2500)]:gear(name,cp)
    for name,cp in [('Arcane Focus Crystal',1000),('Arcane Focus Orb',2000)]:gear(name,cp,96)
    gear('Druidic Focus Staff',500,97,weaponProfile='srd55:quarterstaff',focus='druidic')
    gear('Druidic Focus Mistletoe',100,97,focus='druidic')
    entries['srd55:arcane-focus'].update(weaponProfile='srd55:quarterstaff',focus='arcane')
    for e in entries.values():
        if e['kind']=='tool' and int(e['source']['page'])==93:e['category']='artisan'
        if e['kind']=='tool' and e['id'] in [ident(x) for x in ['Tinkers Tools','Weavers Tools','Woodcarvers Tools']]:e['category']='artisan'
    for name in ['Abyssal','Celestial','Deep Speech','Druidic','Infernal','Primordial','Sylvan','Thieves Cant','Undercommon']:
        add('language',name,20,id='srd55:language-'+slug(name),category='rare')
    for e in entries.values():
        if e['kind']=='language':e.setdefault('category','standard')
    class_positions={name:raw.index(name+'\nCore '+name+' Traits\n') for name in NAMES}
    class_regions={name:raw[class_positions[name]:class_positions[NAMES[i+1]] if i<11 else raw.index('@@PAGE:83@@')] for i,name in enumerate(NAMES)}
    def features_from(text,owner,start_offset):
        # Exclude feature table rows that sit in the middle of a prose feature.
        text=re.sub(r'\n'+re.escape(owner.title())+r' Features\n.*?\n20 \+6[^\n]*\n','\n',text,flags=re.S)
        headers=list(re.finditer(r'^Level (\d+): ([^\n]+)',text,re.M)); result=[]
        for i,m in enumerate(headers):
            name=m[2].strip();level=int(m[1]);end=headers[i+1].start() if i+1<len(headers) else len(text)
            desc=prose(text[m.end():end]);profile=owner+':'+slug(name)
            # Locate the heading in the original region to recover printed page.
            loc=raw.find(m[0],start_offset)
            result.append({'id':ident(owner+' '+name),'name':name,'level':level,'source':source(page_at(loc)),'description':desc,'mechanics':{'handler':'srd55-v2','feature':profile}})
        counts={x['id']:sum(y['id']==x['id'] for y in result) for x in result}
        for x in result:
            if counts[x['id']]>1:x['id']+='-'+str(x['level'])
        return result
    legacy={e['id']:e for e in json.loads(args.legacy.read_text(encoding='utf-8'))}
    for i,name in enumerate(NAMES):
        profile=name.lower(); c=CORE[profile];region=class_regions[name]
        submark=region.index(name+' Subclass:')
        mainregion=region[:submark]
        spelllist=mainregion.find(name+' Spell List\n')
        if spelllist>=0:mainregion=mainregion[:spelllist]
        fs=features_from(mainregion,profile,class_positions[name])
        feat_levels=[4,6,8,12,14,16,19] if profile=='fighter' else [4,8,10,12,16,19] if profile=='rogue' else [4,8,12,16,19]
        # Repeated class ASIs have one prose heading and an explicit list of later grants.
        original=next((x for x in fs if x['name']=='Ability Score Improvement'),None)
        if original:
            for lvl in feat_levels:
                if lvl not in [4,19]:
                    v=copy.deepcopy(original);v['level']=lvl;v['id']+='-'+str(lvl);fs.append(v)
        repeats={'bard':{'Expertise':[9]},'rogue':{'Expertise':[6]},'sorcerer':{'Metamagic':[10,17]},'warlock':{'Mystic Arcanum':[13,15,17]}}
        for feature_name,repeat_levels in repeats.get(profile,{}).items():
            original=next(x for x in fs if x['name']==feature_name)
            for repeat_level in repeat_levels:
                v=copy.deepcopy(original);v['level']=repeat_level;v['id']+='-'+str(repeat_level);fs.append(v)
        fs.sort(key=lambda f:(f['level'],f['id']))
        skills=sorted(e['id'] for e in entries.values() if e['kind']=='skill') if c['skills']=='*' else skill_ids(c['skills'])
        kits=copy.deepcopy(legacy[ident(name)]['kits']) if profile in ['fighter','wizard'] else {'A':c['kit'],'B':kit(c['gold'])}
        if profile=='wizard':kits['A']['items'].remove('srd55:quarterstaff')
        klass=add('class',name,PAGES[i],rulesProfile=profile,hitDie=c['hitDie'],fixedHp=c['hitDie']//2+1,maxLevel=20,saves=c['saves'],skillCount=c['skillCount'],skills=skills,armorTraining=c['armor'],weaponTraining=c['weapons'],primaryAbilities=c['primary'],primaryAbilityMode=c.get('primaryMode','all'),multiclassSkillCount=c.get('mcSkills',0),multiclassSkills=skills if c.get('mcSkills') else [],multiclassTraining={'armor':c.get('mcArmor',[]),'weapons':c.get('mcWeapons',[]),'tools':c.get('mcTools',[])},toolProficiencies=c.get('tools',[]),initialToolChoices=c.get('toolChoices',[]),multiclassToolChoices=c.get('mcToolChoices',[]),kits=kits,casting=casting_for(profile),featLevels=feat_levels,features=fs,progression=progression_for(profile),proficiency=[2+(n-1)//4 for n in range(1,21)],xp=XP)
        subclass=SUBCLASSES[i];subprofile=slug(subclass)
        subfeatures=features_from(region[submark:],subprofile,class_positions[name]+submark)
        add('subclass',subclass,SUBPAGES[i],rulesProfile=subprofile,classId=ident(name),minimumLevel=3,features=subfeatures,notes=[])
    # Remaining catalogs are appended below.
    school_re='Abjuration|Conjuration|Divination|Enchantment|Evocation|Illusion|Necromancy|Transmutation'
    spell_index={}
    for name in NAMES:
        region=class_regions[name];mark=region.find(name+' Spell List\n')
        if mark<0:continue
        region=region[mark:region.index(name+' Subclass:')]
        current_page=page_at(class_positions[name]+mark);current_level=None
        for line in region.splitlines():
            pm=re.match(r'@@PAGE:(\d+)@@',line)
            if pm:current_page=int(pm[1]);continue
            lm=re.match(r'Cantrips \(Level 0 '+name+r' Spells\)',line)
            if lm:current_level=0;continue
            lm=re.match(r'Level (\d+) '+name+r' Spells',line)
            if lm:current_level=int(lm[1]);continue
            match=re.fullmatch(r'(.+) ('+school_re+r') ([CRM, —–-]+)',line.strip())
            if match and current_level is not None:
                title,school,special=match.groups();title=prose(title);sid=ident(title)
                sp=spell_index.setdefault(sid,dict(name=title,level=current_level,school=school,lists=[],listSources=[]))
                assert sp['level']==current_level,(sid,current_level)
                sp['lists'].append(name.lower());sp['listSources'].append(source(current_page))
    spell_region=section(107,175)
    spell_pattern=re.compile(r'^([^\n]+)\n(?:(?:Level ([1-9]) ('+school_re+r'))|(?:('+school_re+r') Cantrip)) \(([^)]+)\)\s*\nCasting Time:',re.M)
    headers=list(spell_pattern.finditer(spell_region))
    if len(headers)!=len(re.findall(r'^Casting Time:',spell_region,re.M)):raise ValueError('At least one spell casting header was not parsed; inspect the source layout.')
    for index,m in enumerate(headers):
        sid=ident(prose(m[1]));refdata=spell_index.get(sid)
        if not refdata:refdata={'name':prose(m[1]).title(),'level':int(m[2] or 0),'school':m[3] or m[4],'lists':[],'listSources':[]}
        header_lists=sorted(x.strip().lower() for x in prose(m[5]).split(','))
        end=headers[index+1].start() if index+1<len(headers) else len(spell_region)
        body=spell_region[m.end()-len('Casting Time:'):end]
        meta=re.search(r'Casting Time:\s*(.*?)\nRange:\s*(.*?)\nComponents?:\s*(.*?)\nDuration:\s*([^\n]+)\n',body,re.S)
        if not meta:raise ValueError('Missing spell header fields: '+sid)
        casting,range_text,components,duration=[prose(x) for x in meta.groups()]
        desc=prose(body[meta.end():])
        source_page=int(re.findall(r'@@PAGE:(\d+)@@',spell_region[:m.start()])[-1]) if '@@PAGE:' in spell_region[:m.start()] else 107
        component_costs=[{'amount':int(n.replace(',','')),'currency':unit.lower()} for n,unit in re.findall(r'worth (?:at least )?([\d,]+)\+? (GP|SP|CP)',components)]
        e=add('spell',refdata['name'],source_page,level=int(m[2] or 0),school=m[3] or m[4],lists=sorted(set(refdata['lists'])|set(header_lists)),tableLists=sorted(set(refdata['lists'])),descriptionLists=header_lists,listSources=refdata['listSources'],castingTime=casting,range=range_text,components=components,duration=duration,ritual='Ritual' in casting,concentration='Concentration' in duration,materialCost=bool(component_costs),componentCosts=component_costs,materialConsumed='consum' in components.lower(),description=desc,effectCoverage='reference-only')
        if sorted(set(refdata['lists']))!=header_lists:e['listDiscrepancy']='Published spell-description class membership differs from the class-list tables; both sources are retained and membership is their union.'
        attack='ranged' if 'ranged spell attack' in desc.lower() else 'melee' if 'melee spell attack' in desc.lower() else ''
        if sid=='srd55:true-strike':attack='weapon'
        e['attackType']=attack
        e['requiresAttackRoll']=bool(attack)
        range_match=re.search(r'^(\d+) feet',range_text)
        e['rangeFeet']=int(range_match[1]) if range_match else 0
        conditional_consumption='material components are consumed' in desc.lower() and not e['materialConsumed']
        e['materialConsumption']={'mode':'on-cast' if e['materialConsumed'] else 'conditional' if conditional_consumption else 'not-consumed','description':components if not conditional_consumption else 'See spell description for the condition under which material components are consumed.'}
        e['savingThrows']=sorted(set(x.lower() for x in re.findall(r'\b(Strength|Dexterity|Constitution|Intelligence|Wisdom|Charisma) saving throw',desc)))
        e['damageTypes']=sorted(set(re.findall(r'\b(Acid|Bludgeoning|Cold|Fire|Force|Lightning|Necrotic|Piercing|Poison|Psychic|Radiant|Slashing|Thunder) damage',desc)))
        e['dealsDamage']=bool(e['damageTypes'])
        for heading,key in [('Using a Higher-Level Spell Slot.','upcastDescription'),('Cantrip Upgrade.','cantripUpgrade')]:
            if heading in desc:e[key]=desc.split(heading,1)[1].strip()
    missing=set(spell_index)-set(entries)
    if missing:raise ValueError('Spell list entries lack descriptions: '+', '.join(sorted(missing)))
    # Complete SRD feats; Magic Initiate is represented by its three spell-list variants.
    feat_names=['Alert','Magic Initiate','Savage Attacker','Skilled','Ability Score Improvement','Grappler','Archery','Defense','Great Weapon Fighting','Two-Weapon Fighting','Boon of Combat Prowess','Boon of Dimensional Travel','Boon of Fate','Boon of Irresistible Offense','Boon of Spell Recall','Boon of the Night Spirit','Boon of Truesight']
    feat_text=section(87,88);feat_headers=[]
    for name in feat_names:
        match=re.search(r'^'+re.escape(name)+r'\n',feat_text,re.M)
        if not match and name=='Savage Attacker':match=re.search(r'^S\s*av\s*age\s*A\s*t\s*t\s*a\s*c\s*ke\s*r\s*\n',feat_text,re.M|re.I)
        if not match:raise ValueError('Missing feat heading '+name)
        feat_headers.append((match.start(),match.end(),name))
    feat_headers.sort()
    for n,(start,end,name) in enumerate(feat_headers):
        text=prose(feat_text[end:feat_headers[n+1][0] if n+1<len(feat_headers) else len(feat_text)])
        # Category headings separating groups are not part of an individual feat.
        text=re.sub(r' (General Feats|Fighting Style Feats|Epic Boon Feats)$','',text)
        category='origin' if n<4 else 'general' if n<6 else 'fighting-style' if n<10 else 'epic-boon'
        page=87 if start<feat_text.index('@@PAGE:88@@') else 88
        variants=['Magic Initiate Cleric','Magic Initiate Druid','Magic Initiate Wizard'] if name=='Magic Initiate' else [name]
        for variant in variants:
            e=add('feat',variant,page,category=category,description=text,notes=[text],repeatable=name in ['Magic Initiate','Skilled','Ability Score Improvement'],prerequisites={})
            if name=='Magic Initiate':e.update(spellList=variant.split()[-1].lower(),repeatGroup='magic-initiate')
            if category=='fighting-style':e['prerequisites']={'requiresFeature':'fighting-style'}
            if category in ['general','epic-boon']:
                e['prerequisites']['minLevel']=4 if category=='general' else 19
                eligible=ABILITIES if name not in ['Grappler','Boon of Irresistible Offense','Boon of Spell Recall'] else ['strength','dexterity'] if name!='Boon of Spell Recall' else ['intelligence','wisdom','charisma']
                e['abilityIncrease']={'points':2 if name=='Ability Score Improvement' else 1,'maxPerAbility':2 if name=='Ability Score Improvement' else 1,'maxScore':20 if category=='general' else 30,'abilities':eligible}
            if name=='Grappler':e['prerequisites']['abilityAny']={'strength':13,'dexterity':13}
            if name=='Boon of Spell Recall':e['prerequisites']['requiresFeature']='spellcasting'
    # Full, individually source-referenced invocation and Metamagic catalogs.
    inv_names=['Agonizing Blast','Armor of Shadows','Ascendant Step','Devil’s Sight','Devouring Blade','Eldritch Mind','Eldritch Smite','Eldritch Spear','Fiendish Vigor','Gaze of Two Minds','Gift of the Depths','Gift of the Protectors','Investment of the Chain Master','Lessons of the First Ones','Lifedrinker','Mask of Many Faces','Master of Myriad Forms','Misty Visions','One with Shadows','Otherworldly Leap','Pact of the Blade','Pact of the Chain','Pact of the Tome','Repelling Blast','Thirsting Blade','Visions of Distant Realms','Whispers of the Grave','Witch Sight']
    meta_names=['Careful Spell','Distant Spell','Empowered Spell','Extended Spell','Heightened Spell','Quickened Spell','Seeking Spell','Subtle Spell','Transmuted Spell','Twinned Spell']
    def named_catalog(kind,names,a,b,start_marker,end_marker):
        text=section(a,b);text=text[text.index(start_marker)+len(start_marker):];text=text[:text.index(end_marker)]
        positions=[]
        for name in names:
            match=re.search(r'^'+re.escape(name)+r'\n',text,re.M)
            if not match:raise ValueError('Missing '+kind+' '+name)
            positions.append((match.start(),match.end(),name))
        positions.sort()
        for i,(start,end,name) in enumerate(positions):
            desc=prose(text[end:positions[i+1][0] if i+1<len(positions) else len(text)])
            marker=re.findall(r'@@PAGE:(\d+)@@',text[:start]);page=int(marker[-1]) if marker else a
            e=add(kind,name,page,description=desc,mechanics={'handler':'srd55-v2','feature':kind+':'+slug(name)})
            if kind=='metamagic':e['sorceryPointCost']=int(re.search(r'Cost: (\d+)',desc)[1])
            else:
                pre=re.match(r'Prerequisite: (.*?)(?= You | Choose | When | Once | As | The | Until | Your |$)',desc)
                prerequisites=pre[1] if pre else ''
                ml=re.search(r'Level (\d+)\+ Warlock',prerequisites)
                e['prerequisites']={'minWarlockLevel':int(ml[1]) if ml else 1,'invocations':[ident(v) for v in inv_names if v in prerequisites]}
                e['prerequisiteDescription']=prerequisites;e['repeatable']='Repeatable.' in desc
                if 'Cantrip' in prerequisites:e['prerequisites']['cantrip']={'list':'warlock','dealsDamage':True,'requiresAttackRoll':'Attack Roll' in prerequisites,'minimumRange':10 if name=='Eldritch Spear' else 0}
    named_catalog('invocation',inv_names,72,74,'Eldritch Invocation Options\n','Warlock Spell List\n')
    named_catalog('metamagic',meta_names,66,67,'Metamagic Options\n','Sorcerer Spell List\n')
    # Parsed stat blocks are data, not automatic encounter simulation.
    creature_text=section(254,364)
    creature_re=re.compile(r'^([^\n]+)\n((?:Tiny|Small|Medium|Large|Huge|Gargantuan)[^\n]+)\nAC (\d+)[^\n]*\nHP (\d+)[^\n]*\nSpeed ([^\n]+)',re.M)
    creature_headers=list(creature_re.finditer(creature_text))
    chain={'Imp','Pseudodragon','Quasit','Skeleton','Sphinx of Wonder','Sprite','Venomous Snake'}
    for i,m in enumerate(creature_headers):
        name=prose(m[1]);body=creature_text[m.start():creature_headers[i+1].start() if i+1<len(creature_headers) else len(creature_text)]
        cr_match=re.search(r'\bCR (\d+(?:/\d+)?) \(XP',body)
        if not cr_match:continue
        nums=cr_match[1].split('/');cr=int(nums[0])/(int(nums[1]) if len(nums)>1 else 1)
        beast='Beast' in m[2] and 'Swarm' not in m[2]
        if not ((beast and cr<=1) or name in chain):continue
        ability_rows=re.findall(r'\b(Str|Dex|Con|Int|Wis|Cha)\s+(\d+)\s+([+−-]\d+)\s+([+−-]\d+)',body,re.I)
        ability_map=dict(zip(['str','dex','con','int','wis','cha'],ABILITIES));ability_values={ability_map[a.lower()]:int(v) for a,v,_,_ in ability_rows[:6]};saves={ability_map[a.lower()]:int(v.replace('−','-')) for a,_,_,v in ability_rows[:6]}
        if len(ability_values)!=6:raise ValueError('Invalid creature abilities: '+name)
        speed={'walk':int(re.match(r'(\d+)',m[5])[1])};speed.update({a.lower():int(v) for a,v in re.findall(r'(Fly|Swim|Climb|Burrow) (\d+)',m[5])})
        skill_match=re.search(r'\nSkills ([^\n]+)',body);skills={}
        if skill_match:
            for s,v in re.findall(r'([A-Za-z ]+) ([+−-]\d+)',skill_match[1]):skills[slug(s.strip())]=int(v.replace('−','-'))
        marker=re.findall(r'@@PAGE:(\d+)@@',creature_text[:m.start()]);page=int(marker[-1]) if marker else 254
        add('creature',name,page,id='srd55:creature-'+slug(name),type='Beast' if beast else m[2].split(' ',1)[1].split(',')[0].split(' (')[0],size=m[2].split()[0],cr=cr,ac=int(m[3]),hp=int(m[4]),speed=speed,abilities=ability_values,saves=saves,skills=skills,description=prose(body),wildShapeEligible=beast and cr<=1,chainFamiliar=name in chain)
    grants={
        'Life Domain':(40,{3:['Aid','Bless','Cure Wounds','Lesser Restoration'],5:['Mass Healing Word','Revivify'],7:['Aura of Life','Death Ward'],9:['Greater Restoration','Mass Cure Wounds']}),
        'Oath of Devotion':(56,{3:['Protection from Evil and Good','Shield of Faith'],5:['Aid','Zone of Truth'],9:['Beacon of Hope','Dispel Magic'],13:['Freedom of Movement','Guardian of Faith'],17:['Commune','Flame Strike']}),
        'Draconic Sorcery':(70,{3:['Alter Self','Chromatic Orb','Command','Dragons Breath'],5:['Fear','Fly'],7:['Arcane Eye','Charm Monster'],9:['Legend Lore','Summon Dragon']}),
        'Fiend Patron':(76,{3:['Burning Hands','Command','Scorching Ray','Suggestion'],5:['Fireball','Stinking Cloud'],7:['Fire Shield','Wall of Fire'],9:['Geas','Insect Plague']})}
    for subclass,(page,levels) in grants.items():
        entries[ident(subclass)]['spellGrants']=[{'level':level,'spells':[ident(n) for n in names],'source':source(page)} for level,names in levels.items()]
    lands={'arid':{3:['Blur','Burning Hands','Fire Bolt'],5:['Fireball'],7:['Blight'],9:['Wall of Stone']},'polar':{3:['Fog Cloud','Hold Person','Ray of Frost'],5:['Sleet Storm'],7:['Ice Storm'],9:['Cone of Cold']},'temperate':{3:['Misty Step','Shocking Grasp','Sleep'],5:['Lightning Bolt'],7:['Freedom of Movement'],9:['Tree Stride']},'tropical':{3:['Acid Splash','Ray of Sickness','Web'],5:['Stinking Cloud'],7:['Polymorph'],9:['Insect Plague']}}
    entries[ident('Circle of the Land')]['landSpells']={land:[{'level':lvl,'spells':[ident(n) for n in names],'source':source(46)} for lvl,names in levels.items()] for land,levels in lands.items()}
    # Species character-level-5 spell grants and explicit per-character scaling.
    for lineage,spell in [('Drow','Darkness'),('High Elf','Misty Step'),('Wood Elf','Pass without Trace'),('Abyssal','Hold Person'),('Chthonic','Ray of Enfeeblement'),('Infernal','Darkness')]:entries[ident(lineage)]['level5Spell']=ident(spell)
    entries['srd55:dragonborn']['progression']={'breathDice':pad([(1,'1d10'),(5,'2d10'),(11,'3d10'),(17,'4d10')]),'flightMinimumLevel':5,'uses':'proficiency'}
    entries['srd55:goliath']['progression']={'largeFormMinimumLevel':5,'uses':'proficiency'}
    args.out.mkdir(parents=True,exist_ok=True)
    attribution='This work includes material from the System Reference Document 5.2.1 ("SRD 5.2.1") by Wizards of the Coast LLC, available at https://www.dndbeyond.com/srd. The SRD 5.2.1 is licensed under the Creative Commons Attribution 4.0 International License, available at https://creativecommons.org/licenses/by/4.0/legalcode.\n\nChanges: Original SRD material has been converted to structured class, subclass, feat, spell, option, equipment, and creature records. Text extraction normalizes typography and line wrapping. Numeric progression tables were transcribed and visually checked against original pages. Mechanical metadata identifies supported evaluator handlers; inclusion of a description does not establish that its effects have been executed or tested. Application code remains separately licensed under BSD-3-Clause.\n'
    manifest={'schemaVersion':1,'id':'srd55-core','version':'2.0.0','edition':'srd55','moduleVersions':['2.0.0'],'name':'SRD 5.2.1 — classes 1–20 and complete spell catalog','publisher':'Wizards of the Coast LLC','origin':'official','license':{'id':'CC-BY-4.0','text':attribution,'url':'https://creativecommons.org/licenses/by/4.0/legalcode'},'dependencies':[],'conflicts':[],'dataFiles':['content.json','magic-items.json'],'sources':[source(n) for n in [1,19,23,24,25,26,28,31,36,41,47,49,53,57,61,64,70,77,83,84,85,86,87,88,89,91,92,93,94,95,96,97,103,104,107,175,254,344,364]],'coverage':{'status':'experimental','dataCoverage':'12 SRD classes and subclasses levels 1–20; all SRD class spell lists and descriptions; feats, invocations, Metamagic; required species and creature records; separate complete magic-item catalog with explicit per-record mechanical coverage','executionCoverage':'Determined by module 2.0.0 evaluator and acceptance tests, independently of data catalog inclusion.'},'sourceArtifact':{'url':URL,'sha256':hashlib.sha256(args.pdf.read_bytes()).hexdigest(),'pages':364}}
    def require_ref(owner,target,expected):
        if target=='srd55:gaming-set':return
        if target not in entries:raise ValueError('Missing reference '+owner+' -> '+target)
        kind=entries[target]['kind']
        if (expected=='equipment' and kind not in ['gear','tool','weapon','armor','shield']) or (expected!='equipment' and kind!=expected):raise ValueError('Wrong reference kind '+owner+' -> '+target+' expected '+expected)
    for e in entries.values():
        for key,expected in [('feat','feat'),('tool','tool'),('classId','class'),('speciesId','species'),('weaponProfile','weapon'),('level1Spell','spell'),('level3Spell','spell'),('level5Spell','spell')]:
            if key in e:require_ref(e['id'],e[key],expected)
        for key,expected in [('skills','skill'),('cantrips','spell'),('toolProficiencies','tool')]:
            if key in e and isinstance(e[key],list):
                for target in e[key]:require_ref(e['id'],target,expected)
        for k in e.get('kits',{}).values():
            for target in k['items']:require_ref(e['id'],target,'equipment')
        grants=e.get('spellGrants',[])+[g for rows in e.get('landSpells',{}).values() for g in rows]
        for g in grants:
            for target in g['spells']:require_ref(e['id'],target,'spell')
        for target in e.get('prerequisites',{}).get('invocations',[]):require_ref(e['id'],target,'invocation')
    catalog=sorted(entries.values(),key=lambda e:(e['kind'],e['name']))
    assert len({e['id'] for e in catalog})==len(catalog)
    for e in catalog:
        if e['kind']=='class':
            assert len(e['casting']['slots'])==20 and all(len(r)==9 for r in e['casting']['slots'])
            assert e['features']
    (args.out/'content.json').write_text(json.dumps(catalog,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
    (args.out/'manifest.json').write_text(json.dumps(manifest,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
    (args.out/'ATTRIBUTION.md').write_text(attribution,encoding='utf-8')
    from collections import Counter
    print(json.dumps(dict(Counter(e['kind'] for e in catalog)),indent=2))
    print('Class/list spell counts:',{n:{l:sum(e['kind']=='spell' and n.lower() in e.get('lists',[]) and e['level']==l for e in catalog) for l in range(10)} for n in NAMES if casting_for(n.lower())['kind']!='none'})

if __name__=='__main__':main()
