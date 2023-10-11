
#include "StdAfx.h"

#include "InterfaceLogic.h"
#include "Runtime.h"
#include "UnitAttribute.h"
#include "terra.h"
#include "Universe.h"
#include "GenericControls.h"
#include "Config.h"

#include "UniverseInterface.h"

#include "GameShell.h"
#include "MissionEdit.h"
#include "CameraManager.h"

#include "PerimeterShellUI.h"

#include "../Sound/PerimeterSound.h"
#include "ExternalShow.h"
#include "fps.h"
#include "../HT/ht.h"

extern int mt_interface_quant;
CShellLogicDispatcher* _pShellDispatcher = 0;

cAutoUnmark _pUnitHover = 0;

extern char  _bCursorVisible;
extern bool  bNoUnitAction;
extern bool  bNoTracking;
extern char _bMenuMode;


inline int _is_track(sRect& rc)
{
	return rc.left || rc.top || rc.right || rc.bottom;
}
inline int rect_width(sRect& rc)
{
	return rc.right - rc.left;
}
inline int rect_height(sRect& rc)
{
	return rc.bottom - rc.top;
}

/////////////////////////////////////////////////////////////////////////////////////////////////
// CShellLogicDispatcher

CShellLogicDispatcher::CShellLogicDispatcher()
{
	m_nEditRegion = editRegionNone;
	m_bCanFlip = true;
	m_bToolzerSizeChangeMode = false;
	_pShellDispatcher = this;
	SetState(STATE_DEFAULT);
	m_nMouseButtonsState = 0;

	m_fMouseCurrentX = 0;
	m_fMouseCurrentY = 0;
	m_fMousePressX = 0;
	m_fMousePressY = 0;

	m_rcTrack.left = m_rcTrack.top = m_rcTrack.right = m_rcTrack.bottom = 0;
	m_rcEnemy.left = m_rcEnemy.top = m_rcEnemy.right = m_rcEnemy.bottom = 0;

	//region
	for(int i = 0;i < GAME_SHELL_SHOW_MAX;i++)
		m_pShowExternal[i] = NULL;
	m_bTolzerFirstClick = true;

	m_nPickAction = SHELL_PICK_NONE;
	m_nPickData = 0;

	m_hFontUnitsLabel = 0;

	m_hCamera = 0;
	m_hScene = 0;
	m_hLight = 0;
	m_hModel = 0;
	m_pUnitInfo = 0;
	pColumnMain=NULL;
	alwaysShowLifeBars = false;
}

CShellLogicDispatcher::~CShellLogicDispatcher()
{
	delete pColumnMain;
	_pShellDispatcher = 0;
}

RegionMetaDispatcher* CShellLogicDispatcher::regionMetaDispatcher() const
{
	return universe()->activeRegionDispatcher();
}

void CShellLogicDispatcher::SetState(int ns)
{
	if((ns == STATE_TRACKING) && bNoTracking)
		ns = STATE_DEFAULT;

	if (ns == STATE_DEFAULT) {
		m_rcTrack.left  = 0;
		m_rcTrack.right = 0;
		m_rcTrack.top   = 0;
		m_rcTrack.bottom= 0;
	}

	m_nState = ns;
}

void CShellLogicDispatcher::quant(bool game_active)
{
	if(_pUnitHover() && !_pUnitHover->alive())
		_pUnitHover = nullptr;
	if(!mt_interface_quant)
		return;

	if(game_active)
	{
		if ((isAltPressed() || alwaysShowLifeBars) && !_bMenuMode && !gameShell->isScriptReelEnabled() && !gameShell->isCutSceneMode()) {
			for (int i = 0; i < universe()->Players.size(); i++) {
				universe()->Players[i]->mark();
			}
		}

		if(m_nEditRegion != editRegionNone)
		{
			if(m_nEditRegion == editRegion1)
				_shellCursorManager.SetActiveCursor(m_bCanFlip && _shellIconManager.getCurrentEnabledOperation() ? CShellCursorManager::workarea_in : CShellCursorManager::workarea_out, 1);

#ifdef _WIN32
			//TODO idk what is this for, apparently for not calling OnMouseMove if mouse is moving
			MSG msg;
			if(!PeekMessage(&msg, hWndVisGeneric, WM_MOUSEMOVE, WM_MOUSEMOVE, PM_NOREMOVE))
#endif
            {
                _shellIconManager.OnMouseMove(m_fMouseCurrentX, m_fMouseCurrentY);
            }
		}

		if (m_nState == STATE_TRACKING) {
			universe()->select.selectInAreaAndCurrentSelection(
							m_fMousePressX - 0.5f,
							m_fMousePressY - 0.5f,
							m_fMouseCurrentX - 0.5f,
							m_fMouseCurrentY - 0.5f,
							isShiftPressed() ? COMMAND_SELECTED_MODE_NEGATIVE : COMMAND_SELECTED_MODE_NONE);
//		} else {
//			universe()->select.selectCurrentSelection();
		}

		if(pColumnMain)
			pColumnMain->quant();

        //If we had hovering unit and mouse isnt moving, check if is still hovering
        const float UNIT_HOVER_INTERVAL = 100.0;
        if (mouseMoveGame && (clockf() - lastMouseMoveHoverCheck) > UNIT_HOVER_INTERVAL) {
            lastMouseMoveHoverCheck = clockf();

            terPlayer* pPlayer = universe()->activePlayer();
            if(pPlayer && !gameShell->BuildingInstallerInited()) //если не тащим здание
            {
                //Find unit we may be hovering in mouse, in case we already are hovering one pass it as prev unit
                terUnitBase* hoveringUnit = universe()->TraceUnit(Vect2f(m_fMouseCurrentX - 0.5f, m_fMouseCurrentY - 0.5f));
                if (hoveringUnit) {
                    _pUnitHover = hoveringUnit;
                    if (_pUnitHover->playerID() == pPlayer->playerID()) {
                        //свой
                        int bMultiSel = GetSelectedUnitsCount() > 1;
                        if (GetSelectedUnit() == _pUnitHover() && !bMultiSel)
                            OnOverUnit(_pUnitHover());
                        else
                            OnOverFriend(_pUnitHover());

                        if (m_nPickAction == SHELL_PICK_UNIT_ATTACK) {
                            _shellCursorManager.SetActiveCursor(CShellCursorManager::no_attack, 1);
                        } else if (m_nPickAction == SHELL_PICK_UNIT_BUILD) {
                            _shellCursorManager.SetActiveCursor(
                                    unitNeedRepairOrBuild(_pUnitHover()) ? CShellCursorManager::sell
                                                                         : CShellCursorManager::sell_no, 1);
                        }
                    } else //if(GetSelectedUnitsCount())
                    {
                        //чужой
                        OnOverEnemy(_pUnitHover());
                    }
                } else {
                    _pUnitHover = nullptr;
                }

                if (_pUnitHover() == nullptr) {
                    OnMouseIdle();

                    if (m_nPickAction == SHELL_PICK_UNIT_BUILD) {
                        _shellCursorManager.SetActiveCursor(CShellCursorManager::sell, 1);
                    } else if (m_nPickAction == SHELL_PICK_UNIT_ATTACK) {
                        terUnitBase* pUnit = GetSelectedUnit();
                        if (pUnit) {
                            if (universe()->select.canSelectionAttackUnit(0))
                                _shellCursorManager.SetActiveCursor(CShellCursorManager::attack, 1);
                            else
                                _shellCursorManager.SetActiveCursor(CShellCursorManager::no_attack, 1);
                        }
                    }
                }
            } else {
                OnMouseIdle();
                _pUnitHover = nullptr;
            }
            
            //Mark it
            if(_pUnitHover()) {
                if((m_nState == STATE_DEFAULT) && _bCursorVisible &&
                   !gameShell->BuildingInstallerInited() && m_nEditRegion == editRegionNone)
                    _pUnitHover->Mark();
            }
        }

	}

	ListQuantResType::iterator i = m_lstQRes.begin();
	while(i != m_lstQRes.end())
	{
		if(!(*i)())
			m_lstQRes.erase(i++);
		else
			++i;
	}

	if (m_nEditRegion != editRegionNone && isControlPressed()) {
        if(!m_bToolzerSizeChangeMode) {
            //начало изменения размера
            m_bToolzerSizeChangeMode = true;
            gameShell->setMousePressControl(Vect2f(m_fMouseCurrentX, m_fMouseCurrentY));
        }
	} else if(m_bToolzerSizeChangeMode) {
        //конец изменения размера
        gameShell->setCursorPosition(Vect2f(gameShell->mousePressControl().x - 0.5f, gameShell->mousePressControl().y - 0.5f));
        gameShell->MouseMove(Vect2f(gameShell->mousePressControl().x - 0.5f, gameShell->mousePressControl().y - 0.5f), Vect2f::ZERO);
        m_bToolzerSizeChangeMode = false;
    }

	if(GetSelectedUnitsCount() == 0)
		SetUnitView(0, false);

	if(m_hModel && !_bMenuMode)
	{
		tvModelPosition.rot() *=Mat3f((XM_PI/15)*frame_time.delta()/small_model_rate, Z_AXIS);
		m_hModel->SetPosition(tvModelPosition);
//		MatXf mat=m_hModel->GetPosition();
//		mat.rot() *=Mat3f((XM_PI/15)*frame_time.delta()/small_model_rate, Z_AXIS);
//		m_hModel->SetPosition(mat);
	}
}

void CShellLogicDispatcher::PreDraw(time_type time)
{
	if(_shellIconManager.IsInterface())
	{
		m_hScene->PreDraw(m_hCamera);
	}
}
void CShellLogicDispatcher::PostDraw()
{
	if(_shellIconManager.IsInterface())
	{
		m_hScene->PostDraw(m_hCamera);
	}
}

void CShellLogicDispatcher::draw()
{
	if(dbgShowCursorPos)
	{
		Vect3f v;
		if(terCamera->cursorTrace(Vect2f(m_fMouseCurrentX - 0.5f, m_fMouseCurrentY  - 0.5f), v))
		{
			XBuffer buffer;
			buffer.SetDigits(0);
			buffer <= v;
			terRenderDevice->SetFont(m_hFontUnitsLabel);
		 	terRenderDevice->OutText(0, 0, buffer, sColor4f(1, 1, 1, 1));
			terRenderDevice->SetFont(0);
		}
	}

#ifdef PERIMETER_DEBUG
	if (universe() && universe()->activePlayer() && universe()->activePlayer()->isAI()) {
		terRenderDevice->SetFont( m_hFontUnitsLabel );
		terRenderDevice->OutText(950,24,universe()->activePlayer()->difficultyPrm().name,sColor4f(1,1,1,1));
		terRenderDevice->SetFont( NULL );
	}
#endif // PERIMETER_DEBUG

	if(_shellIconManager.IsInterface())
	{
		if(!_bMenuMode)
			m_hScene->Draw(m_hCamera);
	}
}

void CShellLogicDispatcher::ResetState()
{
	SetState(STATE_DEFAULT);
}

//mouse events
int CShellLogicDispatcher::OnMouseMove(float x, float y)
{
	if(!mt_interface_quant)
		return 0;
	if(_bMenuMode)
		return 0;
	if (gameShell->isScriptReelEnabled()) {
		return 0;
	}

    mouseMoveGame = false;
	m_fMouseCurrentX = x;
	m_fMouseCurrentY = y;
	bool editWorkArea = (m_nEditRegion != editRegionNone);

	if (editWorkArea) {
		if(isControlPressed())
		{
			if(!m_bToolzerSizeChangeMode)
			{
				//начало изменения размера
				m_bToolzerSizeChangeMode = true;
				gameShell->setMousePressControl(Vect2f(m_fMouseCurrentX, m_fMouseCurrentY));
			}
		}
		_pUnitHover = 0;
		return 0;
	}

	if (m_nState == STATE_TRACKING) {
		//track rect (будет нарисован в draw()
		int x0 = (m_fMousePressX-0.5f)*terScreenSizeX + terScreenSizeX/2;
		int y0 = (m_fMousePressY-0.5f)*terScreenSizeY + terScreenSizeY/2;
		int x1 = (x-0.5f)*terScreenSizeX + terScreenSizeX/2;
		int y1 = (y-0.5f)*terScreenSizeY + terScreenSizeY/2;

		if(x0 > x1)
			std::swap(x0, x1);

		if(y0 > y1)
			std::swap(y0, y1);
		m_rcTrack.left  = x0;
		m_rcTrack.right = x1;
		m_rcTrack.top   = y0;
		m_rcTrack.bottom= y1;
	}

	if(!universe())
		return 0;

    mouseMoveGame = true;
    
	return 0;
}

void CShellLogicDispatcher::setArrowCursor() {
	if (
			_pUnitHover()
		&&	universe()->activePlayer()
		&& _pUnitHover->playerID() != universe()->activePlayer()->playerID() ) {

		_shellCursorManager.SetActiveCursor(CShellCursorManager::arrow_enemy);
	} else {
		_shellCursorManager.SetActiveCursor(CShellCursorManager::arrow);
	}
}

void SoundOnSetUnitTarget(terUnitBase* p)
{
	if(p)
	{
		switch(p->attr()->ID)
		{
		case UNIT_ATTRIBUTE_SQUAD:
//			SND2DPlaySound("squad_moves");
			break;
        default:
            break;
		}
	}
}

bool CShellLogicDispatcher::TranslatePickAction()
{
	bool bRet = false;
	Vect3f v;

	switch(m_nPickAction)
	{
	case SHELL_PICK_UNIT_TARGET:
		if(terCamera->cursorTrace(Vect2f(m_fMouseCurrentX-0.5f, m_fMouseCurrentY-0.5f), v))
		{
			universe()->makeCommandSubtle(COMMAND_ID_POINT, v, COMMAND_SELECTED_MODE_NONE);
			m_nPickAction = SHELL_PICK_NONE;
			setArrowCursor();
			bRet = true;
		}
		break;

	case SHELL_PICK_UNIT_ATTACK:
		if(terCamera->cursorTrace(Vect2f(m_fMouseCurrentX-0.5f, m_fMouseCurrentY-0.5f), v)){
			terUnitBase* pUnit = GetSelectedUnit();
			if(pUnit){
				if(_pUnitHover() && universe()->select.canSelectionAttackUnit(_pUnitHover()))
					universe()->select.makeCommandWithCanAttackFilter(_pUnitHover());
//				else if (_pShellDispatcher->m_nPickData == 1) { // Стреляет по земле
				else { // Стреляет по земле
					if (pUnit->attr()->ID == UNIT_ATTRIBUTE_SQUAD) {
						universe()->select.makeCommandWithCanAttackFilter(v);
					} else {
						universe()->select.makeCommand2DWithCanAttackFilter(v);
//						universe()->makeCommandSubtle(COMMAND_ID_POINT, v, COMMAND_SELECTED_MODE_SINGLE);
					}
				}
			}

			m_nPickAction = SHELL_PICK_NONE;
			m_nPickData = 0;
			setArrowCursor();
			bRet = true;
		}
		break;

	case SHELL_PICK_UNIT_PATROL:
		{
			terUnitBase* pUnit = GetSelectedUnit();
			if(pUnit == 0 || pUnit->attr()->ID != UNIT_ATTRIBUTE_SQUAD)
			{
				m_nPickAction = SHELL_PICK_NONE;
				setArrowCursor();
			}
			else if(terCamera->cursorTrace(Vect2f(m_fMouseCurrentX-0.5f, m_fMouseCurrentY-0.5f), v))
			{
				if(m_nPickData == 1)
				{
					universe()->makeCommandSubtle(COMMAND_ID_PATROL, v, COMMAND_SELECTED_MODE_NONE);
					m_nPickData = 0;
				}
				else
					universe()->makeCommandSubtle(COMMAND_ID_PATROL, v, COMMAND_SELECTED_MODE_NEGATIVE);

				bRet = true;
			}
		}
		break;

	case SHELL_PICK_UNIT_BUILD:
		{
			terPlayer* pPlayer = universe()->activePlayer();
			if (
						pPlayer
					&&	_pUnitHover()
					&&	(_pUnitHover()->Player->isWorld() || _pUnitHover()->playerID() == pPlayer->playerID())
					&&	unitNeedRepairOrBuild(_pUnitHover())
				) {

				universe()->makeCommand(COMMAND_ID_OBJECT, _pUnitHover(), COMMAND_SELECTED_MODE_NONE);
			}

			m_nPickAction = SHELL_PICK_NONE;
			setArrowCursor();
			bRet = true;
		}
		break;
	}

	return bRet;
}

int CShellLogicDispatcher::OnLButtonDown(float x, float y)
{
	if(!universe())
		return 0;

	terPlayer* pPlayer = universe()->activePlayer();
	if(!pPlayer)
		return 0;

	if (gameShell->BuildingInstallerInited()) {
		return 0;
	}

	int ret = 0;

	if (m_nState == STATE_DEFAULT && !(m_nMouseButtonsState & MK_RBUTTON)) {
		//без рамки и тулзеров и правая кнопка не холдится
		if (!TranslatePickAction()) {
			//не выполнялась команда по кнопке
			if (!_pUnitHover()) {
				//на пустом месте
				SetState(STATE_TRACKING);
			} else {
				//над юнитом
				if (_pUnitHover->playerID() == pPlayer->playerID()) {
					//свой
					universe()->select.unitToSelection(_pUnitHover(), isShiftPressed() ? COMMAND_SELECTED_MODE_NEGATIVE : COMMAND_SELECTED_MODE_NONE);
					SoundOnUnitPick(_pUnitHover());
				} else {
					//чужой
				}
			}
		}
	}

	m_fMousePressX = x;
	m_fMousePressY = y;
	m_nMouseButtonsState |= MK_LBUTTON;

	return ret;
}

int CShellLogicDispatcher::OnLButtonUp(float x, float y)
{
	if(!universe())
		return 0;

	terPlayer* pPlayer = universe()->activePlayer();
	if(!pPlayer)
		return 0;

	if((m_nMouseButtonsState & MK_LBUTTON) == 0)
		return 0;

	if (gameShell->BuildingInstallerInited()) {
		return 0;
	}

	int ret = 0;


	if (m_nState == STATE_EDIT_REGION) {
		RegionEndEdit();
	} else {
		if (m_nState == STATE_TRACKING)	{
			universe()->select.areaToSelection(
							m_fMousePressX - 0.5f,
							m_fMousePressY - 0.5f,
							m_fMouseCurrentX - 0.5f,
							m_fMouseCurrentY - 0.5f,
							isShiftPressed() ? COMMAND_SELECTED_MODE_NEGATIVE : COMMAND_SELECTED_MODE_NONE);
			SoundOnUnitPick(_pUnitHover());
		}
	
		OnMouseMove(x, y); //нужно для смены курсора
		SetState(STATE_DEFAULT);
	}
	
	m_rcTrack.left = m_rcTrack.top = m_rcTrack.right = m_rcTrack.bottom = 0;
	m_nMouseButtonsState &= ~MK_LBUTTON;

	return ret;
}
int CShellLogicDispatcher::OnLButtonDblClk(float x, float y)
{
	if (gameShell->BuildingInstallerInited()) {
		return 0;
	}
	if (_pUnitHover()) {
		terPlayer* pPlayer = universe()->activePlayer();
		terFrame* fr = dynamic_cast<terFrame*>(_pUnitHover());
		if (fr && pPlayer && _pUnitHover->playerID() == pPlayer->playerID() && _shellIconManager.GetWnd(SQSH_SELPANEL_FRAME_INSTALL_ID)->isEnabled()) {
			if (!fr->attached() && fr->basementReady()) {
				universe()->makeCommand(COMMAND_ID_FRAME_ATTACH,0);
			}
		} else if (!_pUnitHover()->isBuilding() || isShiftPressed()) {
            //Allow selecting same type unit if not a building or shift is pressed on building
			universe()->select.allLikeUnitToSelection(_pUnitHover());
		}
	}
	return 0;
}

int CShellLogicDispatcher::OnRButtonDown(float x, float y)
{
	if (gameShell->BuildingInstallerInited()) {
		return 0;
	}

	m_fMousePressX = x;
	m_fMousePressY = y;
	m_nMouseButtonsState |= MK_RBUTTON;

	if (m_nState == STATE_TRACKING) {
		SetState(STATE_DEFAULT);
	} else if (m_nState == STATE_DEFAULT) {
		if (
				m_nPickAction == SHELL_PICK_UNIT_PATROL
			||	m_nPickAction == SHELL_PICK_UNIT_TARGET
			||	m_nPickAction == SHELL_PICK_UNIT_ATTACK
			||	m_nPickAction == SHELL_PICK_UNIT_BUILD	)	{

			setArrowCursor();
			m_nPickAction = SHELL_PICK_NONE;
		} else {
			if (!universe()) {
				return 0;
			}

			terPlayer* pPlayer = universe()->activePlayer();
			if (!pPlayer) {
				return 0;
			}
//			if (_pUnitHover() && _pUnitHover->Attribute.ID != UNIT_ATTRIBUTE_STATIC_NATURE && (_pUnitHover->playerID() != pPlayer->playerID())) {
            CommandSelectionMode mode = isShiftPressed() ? COMMAND_SELECTED_MODE_NEGATIVE : COMMAND_SELECTED_MODE_NONE;
            if (isControlPressed()) mode = static_cast<CommandSelectionMode>(mode | COMMAND_SELECTED_MODE_OVERRIDE);
			if (_pUnitHover() && _pUnitHover->attr()->ID != UNIT_ATTRIBUTE_STATIC_NATURE) {
				universe()->makeCommand(COMMAND_ID_OBJECT, _pUnitHover(), mode);
 				//SoundOnSetUnitEnemy(pSelected);
			} else if (GetSelectedUnit() && !GetSelectedUnit()->isBuilding()) {
				Vect3f v;
				if (terCamera->cursorTrace(Vect2f(m_fMouseCurrentX-0.5f, m_fMouseCurrentY-0.5f), v)) {
					universe()->makeCommandSubtle(COMMAND_ID_POINT, v, mode);
					SoundOnSetUnitTarget(GetSelectedUnit());
				}
			}
		}
	}


	return 0;
}
int CShellLogicDispatcher::OnRButtonUp(float x, float y)
{
	if (gameShell->BuildingInstallerInited()) {
		return 0;
	}

	m_nMouseButtonsState &= ~MK_RBUTTON;

	return 0;
}
int CShellLogicDispatcher::OnRButtonDblClk(float x, float y)
{
	return 0;
}

void CShellLogicDispatcher::SoundOnUnitPick(terUnitBase* p)
{
	if(!p)
		return;
	if (p->attr()->ID == UNIT_ATTRIBUTE_TERRAIN_MASTER || p->attr()->ID == UNIT_ATTRIBUTE_BUILD_MASTER) {
		return;
	}

	if(p->GetSquadPoint() && p->attr()->MilitaryUnit)
	{
		p->soundEvent(SOUND_VOICE_SQUAD_SELECTED);
		return;
	}

	if(p->attr()->ID == UNIT_ATTRIBUTE_FRAME){
		terFrame* fp = safe_cast<terFrame*>(p);
		if(fp->powered())
			fp->soundEvent(SOUND_EVENT_SELECT_ALTERNATE);
		else
			fp->soundEvent(SOUND_EVENT_SELECT);
	}
	else
		p->soundEvent(SOUND_EVENT_SELECT);
	p->soundEvent(SOUND_VOICE_UNIT_SELECTED);
}

int CShellLogicDispatcher::GetSelectedUnitsCount(int TypeID)
{
	if(!universe())
		return 0;

	if(!universe()->activePlayer())
		return 0;

	CSELECT_AUTOLOCK();
	if(TypeID < 0)
		return universe()->select.GetSelectList().size();

	int n = 0;
	const UnitList& select_list=universe()->select.GetSelectList();
	UnitList::const_iterator i;
	FOR_EACH(select_list, i)
		if((*i)->attr()->ID == TypeID)
			n++;

	return n;
}
terUnitBase* CShellLogicDispatcher::GetSelectedUnit(int i)
{
	if(!universe()->activePlayer())
		return 0;

	CSELECT_AUTOLOCK();
	const UnitList& sel_list=universe()->select.GetSelectList();
	UnitList::const_iterator i_unit = sel_list.begin();

	while((i-- > 0) && (i_unit++ != sel_list.end()));

	return i_unit != sel_list.end() ? *i_unit : 0;
}
void CShellLogicDispatcher::AddResourceQuant(pfnFQH pfn)
{
	if(find(m_lstQRes.begin(), m_lstQRes.end(), pfn) == m_lstQRes.end())
		m_lstQRes.push_back(pfn);
}
void CShellLogicDispatcher::SetEditRegion(int rg)
{
	m_nEditRegion = rg;
	regionMetaDispatcher()->setActiveLayer(rg - 1);
}


////////////////////////////////////////////////////////////////////////////////////////////////
// модели в окошке 
std::string GetBelligerentTexturePath(terBelligerent belligerent);
void CShellLogicDispatcher::SetUnitView(const AttributeBase* pInfo, bool bPowerOn)
{
	if(m_pUnitInfo == pInfo)
	{
		if(pInfo && m_hModel)
		{
			const InterfaceTV* pModelData = &pInfo->interfaceTV;
			if(bPowerOn && strlen(pModelData->channel_power_on))
				m_hModel->SetChannel(pModelData->channel_power_on, true);
			else if(strlen(pModelData->channel))
				m_hModel->SetChannel(pModelData->channel, true);
			float t = (float)frame_time()/2000.f;
			m_hModel->SetPhase(t - xm::floor(t));
		}

		return;
	}

	m_pUnitInfo = pInfo;
	_RELEASE(m_hModel);

	if(m_pUnitInfo)
	{
		XBuffer xb;
		const InterfaceTV* pModelData = &m_pUnitInfo->interfaceTV;
		const char* modelName = strlen(pModelData->model) ? pModelData->model : pInfo->modelData.modelName;
		if(!modelName || !strlen(modelName))
			return;

		std::string texture_path=GetBelligerentTexturePath(universe()->activePlayer()->belligerent());
		m_hModel = _pShellDispatcher->m_hScene->CreateObject(modelName, texture_path.c_str());

		float fRadius=m_hModel->GetBoundRadius();
		m_hModel->SetScale(Vect3f(pModelData->radius/fRadius, pModelData->radius/fRadius, pModelData->radius/fRadius));

		tvModelPosition = GetMatrix(Vect3f(pModelData->pos_dx,pModelData->pos_dy,0), 
										Vect3f(pModelData->angle_x,pModelData->angle_y,pModelData->angle_z));
		m_hModel->SetPosition(tvModelPosition);
//		m_hModel->SetPosition(GetMatrix(Vect3f(pModelData->pos_dx,pModelData->pos_dy,0), 
//										Vect3f(pModelData->angle_x,pModelData->angle_y,pModelData->angle_z)));

		if(bPowerOn && strlen(pModelData->channel_power_on))
			m_hModel->SetChannel(pModelData->channel_power_on, true);
		else if(strlen(pModelData->channel))
			m_hModel->SetChannel(pModelData->channel, true);

		m_hModel->SetSkinColor(universe()->activePlayer()->unitColor());
	}
}
