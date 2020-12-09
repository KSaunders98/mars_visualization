#pragma once

#include "GLView.h"

namespace Aftr {
    class Camera;

    class GLViewMarsVisualization : public GLView {
    public:
       static GLViewMarsVisualization* New( const std::vector< std::string >& outArgs );
       virtual ~GLViewMarsVisualization();
       virtual void updateWorld(); ///< Called once per frame
       virtual void loadMap(); ///< Called once at startup to build this module's scene
       //virtual void onResizeWindow( GLsizei width, GLsizei height );
       //virtual void onMouseDown( const SDL_MouseButtonEvent& e );
       //virtual void onMouseUp( const SDL_MouseButtonEvent& e );
       //virtual void onMouseMove( const SDL_MouseMotionEvent& e );
       //virtual void onKeyDown( const SDL_KeyboardEvent& key );
       //virtual void onKeyUp( const SDL_KeyboardEvent& key );

    protected:
       GLViewMarsVisualization( const std::vector< std::string >& args );
       virtual void onCreate();   
    };
}
