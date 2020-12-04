#include "GLViewMarsVisualization.h"

#include "Axes.h"
#include "ManagerOpenGLState.h"
#include "WorldList.h"

#include "AftrGLRendererBase.h"
#include "Camera.h"
#include "Model.h"
#include "Utils.h"
#include "WO.h"
#include "WOMars.h"
#include "WOLight.h"
#include "WOSkyBox.h"

#include "cpprest/filestream.h"
#include "cpprest/http_client.h"

using namespace Aftr;

using namespace web::http;
using namespace web::http::client;

constexpr double SCALE = 1;

GLViewMarsVisualization* GLViewMarsVisualization::New( const std::vector< std::string >& args )
{
    GLViewMarsVisualization* glv = new GLViewMarsVisualization( args );
    glv->init( Aftr::GRAVITY, Vector( 0, 0, -1.0f ), "aftr.conf", PHYSICS_ENGINE_TYPE::petODE );
    glv->onCreate();
    return glv;
}

GLViewMarsVisualization::GLViewMarsVisualization( const std::vector< std::string >& args ) : GLView( args )
{
    //Initialize any member variables that need to be used inside of LoadMap() here.
    //Note: At this point, the Managers are not yet initialized. The Engine initialization
    //occurs immediately after this method returns (see GLViewMarsVisualization::New() for
    //reference). Then the engine invoke's GLView::loadMap() for this module.
    //After loadMap() returns, GLView::onCreate is finally invoked.

    //The order of execution of a module startup:
    //GLView::New() is invoked:
    //     calls GLView::init()
    //         calls GLView::loadMap() (as well as initializing the engine's Managers)
    //     calls GLView::onCreate()

    //GLViewMarsVisualization::onCreate() is invoked after this module's LoadMap() is completed.
}


void GLViewMarsVisualization::onCreate()
{
    //GLViewMarsVisualization::onCreate() is invoked after this module's LoadMap() is completed.
    //At this point, all the managers are initialized. That is, the engine is fully initialized.

    if( this->pe != NULL )
    {
        //optionally, change gravity direction and magnitude here
        //The user could load these values from the module's aftr.conf
        this->pe->setGravityNormalizedVector( Vector( 0,0,-1.0f ) );
        this->pe->setGravityScalar( Aftr::GRAVITY );
    }
    this->setActorChaseType( STANDARDEZNAV ); //Default is STANDARDEZNAV mode
    //this->setNumPhysicsStepsPerRender( 0 ); //pause physics engine on start up; will remain paused till set to 1
}


GLViewMarsVisualization::~GLViewMarsVisualization()
{
    //Implicitly calls GLView::~GLView()
}


void GLViewMarsVisualization::updateWorld()
{
    GLView::updateWorld(); //Just call the parent's update world first.
                                  //If you want to add additional functionality, do it after
                                  //this call.
}


void GLViewMarsVisualization::onResizeWindow( GLsizei width, GLsizei height )
{
    GLView::onResizeWindow( width, height ); //call parent's resize method.
}


void GLViewMarsVisualization::onMouseDown( const SDL_MouseButtonEvent& e )
{
    GLView::onMouseDown( e );
}


void GLViewMarsVisualization::onMouseUp( const SDL_MouseButtonEvent& e )
{
    GLView::onMouseUp( e );
}


void GLViewMarsVisualization::onMouseMove( const SDL_MouseMotionEvent& e )
{
    GLView::onMouseMove( e );
}


void GLViewMarsVisualization::onKeyDown( const SDL_KeyboardEvent& key )
{
    GLView::onKeyDown( key );
    if( key.keysym.sym == SDLK_0 )
        this->setNumPhysicsStepsPerRender( 1 );

    if( key.keysym.sym == SDLK_1 )
    {
        http_client client(L"http://192.168.1.110:3000/elevation");
        uri_builder builder{};
        builder.append_query(L"id", "0");

        http_response response;
        try {
            response = client.request(methods::GET, builder.to_string()).get();
        } catch (...) {
            std::cerr << "Unable to fetch elevation: " << (client.base_uri().to_string() + builder.to_string()).c_str() << std::endl;
            return;
        }


        if (response.status_code() != status_codes::OK) {
            std::cerr << "Unable to fetch elevation: " << (client.base_uri().to_string() + builder.to_string()).c_str()
                      << "\n\tStatus Code: " << response.status_code() << std::endl;
            return;
        }

        std::vector<unsigned char> data;
        try {
            data = response.extract_vector().get();
        } catch (...) {
            std::cerr << "Unable to fetch elevation: " << (client.base_uri().to_string() + builder.to_string()).c_str()
                      << "\n\tUnable to get data from response" << std::endl;
            return;
        }

        if (data.size() != 256 * 256 * 2) {
            std::cerr << "Unable to fetch elevation: " << (client.base_uri().to_string() + builder.to_string()).c_str()
                      << "\n\tIncorrect response size: " << data.size() << " bytes (expected " << 256 * 256 * 2 << " bytes)" << std::endl;
            return;
        }

        std::vector<int16_t> elev;
        elev.reserve(256 * 256);
        
        for (size_t i = 0; i < data.size(); i += 2) {
            // bytes are in big-endian int16 format
            int16_t e = static_cast<int16_t>(data[i]) << 8 | static_cast<int16_t>(data[i + 1]);
            elev.push_back(e);
        }

        std::cout << "Success" << std::endl;
    }
}


void GLViewMarsVisualization::onKeyUp( const SDL_KeyboardEvent& key )
{
    GLView::onKeyUp( key );
}


void Aftr::GLViewMarsVisualization::loadMap()
{
    this->worldLst = new WorldList(); //WorldList is a 'smart' vector that is used to store WO*'s
    this->actorLst = new WorldList();
    this->netLst = new WorldList();

    ManagerOpenGLState::GL_CLIPPING_PLANE = 100000.0;
    ManagerOpenGLState::GL_NEAR_PLANE = 1.0f;
    ManagerOpenGLState::enableFrustumCulling = false;
    Axes::isVisible = true;
    this->glRenderer->isUsingShadowMapping( false ); //set to TRUE to enable shadow mapping, must be using GL 3.2+

    this->cam->setPosition( 15,15,10 );
    
    //SkyBox Textures readily available
    std::vector< std::string > skyBoxImageNames; //vector to store texture paths
    //skyBoxImageNames.push_back( ManagerEnvironmentConfiguration::getSMM() + "/images/skyboxes/sky_water+6.jpg" );
    //skyBoxImageNames.push_back( ManagerEnvironmentConfiguration::getSMM() + "/images/skyboxes/sky_dust+6.jpg" );
    skyBoxImageNames.push_back( ManagerEnvironmentConfiguration::getSMM() + "/images/skyboxes/sky_mountains+6.jpg" );
    //skyBoxImageNames.push_back( ManagerEnvironmentConfiguration::getSMM() + "/images/skyboxes/sky_winter+6.jpg" );
    //skyBoxImageNames.push_back( ManagerEnvironmentConfiguration::getSMM() + "/images/skyboxes/early_morning+6.jpg" );
    //skyBoxImageNames.push_back( ManagerEnvironmentConfiguration::getSMM() + "/images/skyboxes/sky_afternoon+6.jpg" );
    //skyBoxImageNames.push_back( ManagerEnvironmentConfiguration::getSMM() + "/images/skyboxes/sky_cloudy+6.jpg" );
    //skyBoxImageNames.push_back( ManagerEnvironmentConfiguration::getSMM() + "/images/skyboxes/sky_cloudy3+6.jpg" );
    //skyBoxImageNames.push_back( ManagerEnvironmentConfiguration::getSMM() + "/images/skyboxes/sky_day+6.jpg" );
    //skyBoxImageNames.push_back( ManagerEnvironmentConfiguration::getSMM() + "/images/skyboxes/sky_day2+6.jpg" );
    //skyBoxImageNames.push_back( ManagerEnvironmentConfiguration::getSMM() + "/images/skyboxes/sky_deepsun+6.jpg" );
    //skyBoxImageNames.push_back( ManagerEnvironmentConfiguration::getSMM() + "/images/skyboxes/sky_evening+6.jpg" );
    //skyBoxImageNames.push_back( ManagerEnvironmentConfiguration::getSMM() + "/images/skyboxes/sky_morning+6.jpg" );
    //skyBoxImageNames.push_back( ManagerEnvironmentConfiguration::getSMM() + "/images/skyboxes/sky_morning2+6.jpg" );
    //skyBoxImageNames.push_back( ManagerEnvironmentConfiguration::getSMM() + "/images/skyboxes/sky_noon+6.jpg" );
    //skyBoxImageNames.push_back( ManagerEnvironmentConfiguration::getSMM() + "/images/skyboxes/sky_warp+6.jpg" );
    //skyBoxImageNames.push_back( ManagerEnvironmentConfiguration::getSMM() + "/images/skyboxes/space_Hubble_Nebula+6.jpg" );
    //skyBoxImageNames.push_back( ManagerEnvironmentConfiguration::getSMM() + "/images/skyboxes/space_gray_matter+6.jpg" );
    //skyBoxImageNames.push_back( ManagerEnvironmentConfiguration::getSMM() + "/images/skyboxes/space_easter+6.jpg" );
    //skyBoxImageNames.push_back( ManagerEnvironmentConfiguration::getSMM() + "/images/skyboxes/space_hot_nebula+6.jpg" );
    //skyBoxImageNames.push_back( ManagerEnvironmentConfiguration::getSMM() + "/images/skyboxes/space_ice_field+6.jpg" );
    //skyBoxImageNames.push_back( ManagerEnvironmentConfiguration::getSMM() + "/images/skyboxes/space_lemon_lime+6.jpg" );
    //skyBoxImageNames.push_back( ManagerEnvironmentConfiguration::getSMM() + "/images/skyboxes/space_milk_chocolate+6.jpg" );
    //skyBoxImageNames.push_back( ManagerEnvironmentConfiguration::getSMM() + "/images/skyboxes/space_solar_bloom+6.jpg" );
    //skyBoxImageNames.push_back( ManagerEnvironmentConfiguration::getSMM() + "/images/skyboxes/space_thick_rb+6.jpg" );

    float ga = 0.1f; //Global Ambient Light level for this module
    ManagerLight::setGlobalAmbientLight( aftrColor4f( ga, ga, ga, 1.0f ) );
    WOLight* light = WOLight::New();
    light->isDirectionalLight( true );
    light->setPosition( Vector( 0, 0, 100 ) );
    //Set the light's display matrix such that it casts light in a direction parallel to the -z axis (ie, downwards as though it was "high noon")
    //for shadow mapping to work, this->glRenderer->isUsingShadowMapping( true ), must be invoked.
    light->getModel()->setDisplayMatrix( Mat4::rotateIdentityMat( { 0, 1, 0 }, 90.0f * Aftr::DEGtoRAD ) );
    light->setLabel( "Light" );
    worldLst->push_back( light );

    //Create the SkyBox
    WO* wo = WOSkyBox::New( skyBoxImageNames.at( 0 ), this->getCameraPtrPtr() );
    wo->setPosition( Vector( 0,0,0 ) );
    wo->setLabel( "Sky Box" );
    wo->renderOrderType = RENDER_ORDER_TYPE::roOPAQUE;
    worldLst->push_back( wo );

    VectorD loc(18.65, -133.8, 2);
    //VectorD loc(-8.88, -92.27, 2);

    WOMars* mars = WOMars::New(const_cast<const Camera**>(getCameraPtrPtr()), loc, SCALE);
    mars->setPosition(0, 0, 0);
    worldLst->push_back(mars);
}
