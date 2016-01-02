#include "GraphicLoader.h"
#include "3dAnimation.h"
#include "Text.h"
#include "Timer.h"
#include "SpriteManager.h"
#include "ResourceManager.h"
#include "png.h"

/************************************************************************/
/* Models                                                               */
/************************************************************************/

StrVec  GraphicLoader::processedFiles;
BoneVec GraphicLoader::loadedModels;
StrVec  GraphicLoader::loadedModelNames;
PtrVec  GraphicLoader::loadedAnimations;

void GraphicLoader::DestroyModel( Bone* root_bone )
{
    for( size_t i = 0, j = loadedModels.size(); i < j; i++ )
    {
        if( loadedModels[ i ] == root_bone )
        {
            processedFiles.erase( std::find( processedFiles.begin(), processedFiles.end(), loadedModelNames[ i ] ) );
            loadedModels.erase( loadedModels.begin() + i );
            loadedModelNames.erase( loadedModelNames.begin() + i );
            break;
        }
    }
    delete root_bone;
}

Bone* GraphicLoader::LoadModel( const char* fname )
{
    // Find already loaded
    for( size_t i = 0, j = loadedModelNames.size(); i < j; i++ )
    {
        if( Str::CompareCase( loadedModelNames[ i ].c_str(), fname ) )
            return loadedModels[ i ];
    }

    // Add to already processed
    for( size_t i = 0, j = processedFiles.size(); i < j; i++ )
        if( Str::CompareCase( processedFiles[ i ].c_str(), fname ) )
            return nullptr;
    processedFiles.push_back( fname );

    // Load file data
    FileManager file;
    if( !file.LoadFile( fname, PT_DATA ) )
    {
        WriteLogF( _FUNC_, " - 3d file '%s' not found.\n", fname );
        return nullptr;
    }

    // Load bones
    Bone* root_bone = new Bone();
    root_bone->Load( file );
    root_bone->FixAfterLoad( root_bone );

    // Load animations
    uint anim_sets_count = file.GetBEUInt();
    for( uint i = 0; i < anim_sets_count; i++ )
    {
        AnimSet* anim_set = new AnimSet();
        anim_set->Load( file );
        loadedAnimations.push_back( anim_set );
    }

    // Add to collection
    loadedModels.push_back( root_bone );
    loadedModelNames.push_back( fname );
    return root_bone;
}

AnimSet* GraphicLoader::LoadAnimation( const char* anim_fname, const char* anim_name )
{
    // Find in already loaded
    bool take_first = Str::CompareCase( anim_name, "Base" );
    for( uint i = 0, j = (uint) loadedAnimations.size(); i < j; i++ )
    {
        AnimSet* anim = (AnimSet*) loadedAnimations[ i ];
        if( Str::CompareCase( anim->GetFileName(), anim_fname ) && ( Str::CompareCase( anim->GetName(), anim_name ) || take_first ) )
            return anim;
    }

    // Check maybe file already processed and nothing founded
    for( size_t i = 0, j = processedFiles.size(); i < j; i++ )
        if( Str::CompareCase( processedFiles[ i ].c_str(), anim_fname ) )
            return nullptr;

    // File not processed, load and recheck animations
    if( LoadModel( anim_fname ) )
        return LoadAnimation( anim_fname, anim_name );

    return nullptr;
}

/************************************************************************/
/* Textures                                                             */
/************************************************************************/

MeshTextureVec GraphicLoader::loadedMeshTextures;

MeshTexture* GraphicLoader::LoadTexture( const char* texture_name, const char* model_path )
{
    if( !texture_name || !texture_name[ 0 ] )
        return nullptr;

    // Try find already loaded texture
    for( auto it = loadedMeshTextures.begin(), end = loadedMeshTextures.end(); it != end; ++it )
    {
        MeshTexture* texture = *it;
        if( Str::CompareCase( texture->Name.c_str(), texture_name ) )
            return texture && texture->Id ? texture : nullptr;
    }

    // Allocate structure
    MeshTexture* mesh_tex = new MeshTexture();
    mesh_tex->Name = texture_name;
    mesh_tex->Id = 0;
    loadedMeshTextures.push_back( mesh_tex );

    // First try load from textures folder
    SprMngr.PushAtlasType( RES_ATLAS_TEXTURES );
    AnyFrames* anim = SprMngr.LoadAnimation( texture_name, PT_TEXTURES );
    if( !anim && model_path )
    {
        // After try load from file folder
        char path[ MAX_FOPATH ];
        FileManager::ExtractDir( model_path, path );
        Str::Append( path, texture_name );
        anim = SprMngr.LoadAnimation( path, PT_DATA );
    }
    SprMngr.PopAtlasType();
    if( !anim )
        return nullptr;

    SpriteInfo* si = SprMngr.GetSpriteInfo( anim->Ind[ 0 ] );
    mesh_tex->Id = si->Atlas->TextureOwner->Id;
    memcpy( mesh_tex->SizeData, si->Atlas->TextureOwner->SizeData, sizeof( mesh_tex->SizeData ) );
    mesh_tex->AtlasOffsetData[ 0 ] = si->SprRect[ 0 ];
    mesh_tex->AtlasOffsetData[ 1 ] = si->SprRect[ 1 ];
    mesh_tex->AtlasOffsetData[ 2 ] = si->SprRect[ 2 ] - si->SprRect[ 0 ];
    mesh_tex->AtlasOffsetData[ 3 ] = si->SprRect[ 3 ] - si->SprRect[ 1 ];
    AnyFrames::Destroy( anim );
    return mesh_tex;
}

void GraphicLoader::DestroyTextures()
{
    for( auto it = loadedMeshTextures.begin(), end = loadedMeshTextures.end(); it != end; ++it )
        delete *it;
    loadedMeshTextures.clear();
}

/************************************************************************/
/* Effects                                                              */
/************************************************************************/

EffectVec GraphicLoader::loadedEffects;

Effect* GraphicLoader::LoadEffect( const char* effect_name, bool use_in_2d, const char* defines /* = NULL */, const char* model_path /* = NULL */, EffectDefault* defaults /* = NULL */, uint defaults_count /* = 0 */ )
{
    // Erase extension
    char fname[ MAX_FOPATH ];
    Str::Copy( fname, effect_name );
    FileManager::EraseExtension( fname );

    // Reset defaults to NULL if it's count is zero
    if( defaults_count == 0 )
        defaults = nullptr;

    // Try find already loaded effect
    char loaded_fname[ MAX_FOPATH ];
    Str::Copy( loaded_fname, fname );
    for( auto it = loadedEffects.begin(), end = loadedEffects.end(); it != end; ++it )
    {
        Effect* effect = *it;
        if( Str::CompareCase( effect->Name.c_str(), loaded_fname ) && Str::Compare( effect->Defines.c_str(), defines ? defines : "" ) && effect->Defaults == defaults )
            return effect;
    }

    // Add extension
    Str::Append( fname, ".glsl" );

    // Load text file
    FileManager file;
    if( !file.LoadFile( fname, PT_EFFECTS ) && model_path )
    {
        // Try load from model folder
        char path[ MAX_FOPATH ];
        FileManager::ExtractDir( model_path, path );
        Str::Append( path, fname );
        file.LoadFile( path, PT_DATA );
    }
    if( !file.IsLoaded() )
    {
        WriteLogF( _FUNC_, " - Effect file '%s' not found.\n", fname );
        return nullptr;
    }

    // Parse effect commands
    vector< StrVec > commands;
    char             line[ MAX_FOTEXT ];
    while( file.GetLine( line, sizeof( line ) ) )
    {
        Str::Trim( line );
        if( Str::CompareCaseCount( line, "Effect ", Str::Length( "Effect " ) ) )
        {
            StrVec tokens;
            Str::ParseLine( line + Str::Length( "Effect " ), ' ', tokens, Str::ParseLineDummy );
            if( !tokens.empty() )
                commands.push_back( tokens );
        }
        else if( Str::CompareCaseCount( line, "#version", Str::Length( "#version" ) ) )
        {
            break;
        }
    }

    // Find passes count
    bool fail = false;
    uint passes = 1;
    for( size_t i = 0; i < commands.size(); i++ )
        if( commands[ i ].size() >= 2 && Str::CompareCase( commands[ i ][ 0 ].c_str(), "Passes" ) )
            passes = ConvertParamValue( commands[ i ][ 1 ].c_str(), fail );

    // New effect
    Effect effect;
    effect.Name = loaded_fname;
    effect.Defines = ( defines ? defines : "" );

    // Load passes
    for( uint pass = 0; pass < passes; pass++ )
        if( !LoadEffectPass( &effect, fname, file, pass, use_in_2d, defines, defaults, defaults_count ) )
            return nullptr;

    // Process commands
    for( size_t i = 0; i < commands.size(); i++ )
    {
        static auto get_gl_blend_func = [] (const char* s)
        {
            if( Str::Compare( s, "GL_ZERO" ) )
                return GL_ZERO;
            if( Str::Compare( s, "GL_ONE" ) )
                return GL_ONE;
            if( Str::Compare( s, "GL_SRC_COLOR" ) )
                return GL_SRC_COLOR;
            if( Str::Compare( s, "GL_ONE_MINUS_SRC_COLOR" ) )
                return GL_ONE_MINUS_SRC_COLOR;
            if( Str::Compare( s, "GL_DST_COLOR" ) )
                return GL_DST_COLOR;
            if( Str::Compare( s, "GL_ONE_MINUS_DST_COLOR" ) )
                return GL_ONE_MINUS_DST_COLOR;
            if( Str::Compare( s, "GL_SRC_ALPHA" ) )
                return GL_SRC_ALPHA;
            if( Str::Compare( s, "GL_ONE_MINUS_SRC_ALPHA" ) )
                return GL_ONE_MINUS_SRC_ALPHA;
            if( Str::Compare( s, "GL_DST_ALPHA" ) )
                return GL_DST_ALPHA;
            if( Str::Compare( s, "GL_ONE_MINUS_DST_ALPHA" ) )
                return GL_ONE_MINUS_DST_ALPHA;
            if( Str::Compare( s, "GL_CONSTANT_COLOR" ) )
                return GL_CONSTANT_COLOR;
            if( Str::Compare( s, "GL_ONE_MINUS_CONSTANT_COLOR" ) )
                return GL_ONE_MINUS_CONSTANT_COLOR;
            if( Str::Compare( s, "GL_CONSTANT_ALPHA" ) )
                return GL_CONSTANT_ALPHA;
            if( Str::Compare( s, "GL_ONE_MINUS_CONSTANT_ALPHA" ) )
                return GL_ONE_MINUS_CONSTANT_ALPHA;
            if( Str::Compare( s, "GL_SRC_ALPHA_SATURATE" ) )
                return GL_SRC_ALPHA_SATURATE;
            return -1;
        };
        static auto get_gl_blend_equation = [] (const char* s)
        {
            if( Str::Compare( s, "GL_FUNC_ADD" ) )
                return GL_FUNC_ADD;
            if( Str::Compare( s, "GL_FUNC_SUBTRACT" ) )
                return GL_FUNC_SUBTRACT;
            if( Str::Compare( s, "GL_FUNC_REVERSE_SUBTRACT" ) )
                return GL_FUNC_REVERSE_SUBTRACT;
            if( Str::Compare( s, "GL_MAX" ) )
                return GL_MAX;
            if( Str::Compare( s, "GL_MIN" ) )
                return GL_MIN;
            return -1;
        };

        StrVec& tokens = commands[ i ];
        if( tokens[ 0 ] == "Pass" && tokens.size() >= 3 )
        {
            uint pass = ConvertParamValue( tokens[ 1 ].c_str(), fail );
            if( pass < passes )
            {
                EffectPass& effect_pass = effect.Passes[ pass ];
                if( tokens[ 2 ] == "BlendFunc" && tokens.size() >= 5 )
                {
                    effect_pass.IsNeedProcess = effect_pass.IsChangeStates = true;
                    effect_pass.BlendFuncParam1 = get_gl_blend_func( tokens[ 3 ].c_str() );
                    effect_pass.BlendFuncParam2 = get_gl_blend_func( tokens[ 4 ].c_str() );
                    if( effect_pass.BlendFuncParam1 == -1 || effect_pass.BlendFuncParam2 == -1 )
                        fail = true;
                }
                else if( tokens[ 2 ] == "BlendEquation" && tokens.size() >= 4 )
                {
                    effect_pass.IsNeedProcess = effect_pass.IsChangeStates = true;
                    effect_pass.BlendEquation = get_gl_blend_equation( tokens[ 3 ].c_str() );
                    if( effect_pass.BlendEquation == -1 )
                        fail = true;
                }
                else if( tokens[ 2 ] == "Shadow" )
                {
                    effect_pass.IsShadow = true;
                }
                else
                {
                    fail = true;
                }
            }
            else
            {
                fail = true;
            }
        }
    }
    if( fail )
    {
        WriteLogF( _FUNC_, " - Invalid commands in effect '%s'.\n", fname );
        return nullptr;
    }

    // Assign identifier and return
    static uint effect_id = 0;
    effect.Id = ++effect_id;
    loadedEffects.push_back( new Effect( effect ) );
    return loadedEffects.back();
}

bool GraphicLoader::LoadEffectPass( Effect* effect, const char* fname, FileManager& file, uint pass, bool use_in_2d, const char* defines, EffectDefault* defaults, uint defaults_count )
{
    EffectPass effect_pass;
    memzero( &effect_pass, sizeof( effect_pass ) );
    GLuint     program = 0;

    // Make effect binary file name
    char binary_fname[ MAX_FOPATH ] = { 0 };
    if( GL_HAS( get_program_binary ) )
    {
        Str::Copy( binary_fname, fname );
        FileManager::EraseExtension( binary_fname );
        if( defines )
        {
            char binary_fname_defines[ MAX_FOPATH ];
            Str::Copy( binary_fname_defines, defines );
            Str::Replacement( binary_fname_defines, '\t', ' ' );                   // Tabs to spaces
            Str::Replacement( binary_fname_defines, ' ', ' ', ' ' );               // Multiple spaces to single
            Str::Replacement( binary_fname_defines, '\r', '\n', '_' );             // EOL's to '_'
            Str::Replacement( binary_fname_defines, '\r', '_' );                   // EOL's to '_'
            Str::Replacement( binary_fname_defines, '\n', '_' );                   // EOL's to '_'
            Str::Append( binary_fname, "_" );
            Str::Append( binary_fname, binary_fname_defines );
        }
        Str::Append( binary_fname, "_" );
        Str::Append( binary_fname, Str::UItoA( pass ) );
        Str::Append( binary_fname, ".glslb" );
    }

    // Load from binary
    FileManager file_binary;
    if( GL_HAS( get_program_binary ) )
    {
        if( file_binary.LoadFile( binary_fname, PT_CACHE ) )
        {
            if( file.GetWriteTime() > file_binary.GetWriteTime() )
                file_binary.UnloadFile();                      // Disable loading from this binary, because its outdated
        }
    }
    if( file_binary.IsLoaded() )
    {
        bool loaded = false;
        uint version = file_binary.GetBEUInt();
        if( version == FONLINE_VERSION )
        {
            GLenum  format = file_binary.GetBEUInt();
            UNUSED_VARIABLE( format );             // OGL ES
            GLsizei length = file_binary.GetBEUInt();
            if( file_binary.GetFsize() >= length + sizeof( uint ) * 3 )
            {
                GL( program = glCreateProgram() );
                glProgramBinary( program, format, file_binary.GetCurBuf(), length );
                glGetError();                 // Skip error from glProgramBinary, if it has
                GLint linked;
                GL( glGetProgramiv( program, GL_LINK_STATUS, &linked ) );
                if( linked )
                {
                    loaded = true;
                }
                else
                {
                    WriteLogF( _FUNC_, " - Failed to link binary shader program '%s', effect '%s'.\n", binary_fname, fname );
                    GL( glDeleteProgram( program ) );
                }
            }
            else
            {
                WriteLogF( _FUNC_, " - Binary shader program '%s' truncated, effect '%s'.\n", binary_fname, fname );
            }
        }
        if( !loaded )
            file_binary.UnloadFile();
    }

    // Load from text
    if( !file_binary.IsLoaded() )
    {
        // Get version
        CharVec file_buf;
        file_buf.resize( file.GetFsize() + 1 );
        Str::Copy( &file_buf[ 0 ], (uint) file_buf.size(), (char*) file.GetBuf() );
        char* str = &file_buf[ 0 ];
        Str::Replacement( str, '\r', '\n', '\n' );
        char* ver = Str::Substring( str, "#version" );
        if( ver )
        {
            char* ver_end = Str::Substring( ver, "\n" );
            if( ver_end )
            {
                str = ver_end + 1;
                *ver_end = 0;
            }
        }
        #ifdef FO_OGL_ES
        char ios_data[] = { "precision lowp float;\n" };
        ver = ios_data;
        #endif

        // Internal definitions
        char internal_defines[ MAX_FOTEXT ];
        Str::Format( internal_defines, "#define PASS%u\n#define MAX_BONES %u\n", pass, Effect::MaxBones );

        // Create shaders
        GLuint vs, fs;
        GL( vs = glCreateShader( GL_VERTEX_SHADER ) );
        GL( fs = glCreateShader( GL_FRAGMENT_SHADER ) );
        CharVec       buf( file_buf.size() + MAX_FOTEXT );
        Str::Format( &buf[ 0 ], "%s%s%s%s%s%s%s", ver ? ver : "", "\n", "#define VERTEX_SHADER\n", internal_defines, defines ? defines : "", "\n", str );
        const GLchar* vs_str = &buf[ 0 ];
        GL( glShaderSource( vs, 1, &vs_str, nullptr ) );
        Str::Format( &buf[ 0 ], "%s%s%s%s%s%s%s", ver ? ver : "", "\n", "#define FRAGMENT_SHADER\n", internal_defines, defines ? defines : "", "\n", str );
        const GLchar* fs_str = &buf[ 0 ];
        GL( glShaderSource( fs, 1, &fs_str, nullptr ) );

        // Info parser
        struct ShaderInfo
        {
            static void Log( const char* shader_name, GLint shader )
            {
                int len = 0;
                GL( glGetShaderiv( shader, GL_INFO_LOG_LENGTH, &len ) );
                if( len > 0 )
                {
                    GLchar* str = new GLchar[ len ];
                    int     chars = 0;
                    glGetShaderInfoLog( shader, len, &chars, str );
                    WriteLog( "%s output:\n%s", shader_name, str );
                    delete[] str;
                }
            }
            static void LogProgram( GLint program )
            {
                int len = 0;
                GL( glGetProgramiv( program, GL_INFO_LOG_LENGTH, &len ) );
                if( len > 0 )
                {
                    GLchar* str = new GLchar[ len ];
                    int     chars = 0;
                    glGetProgramInfoLog( program, len, &chars, str );
                    WriteLog( "Program info output:\n%s", str );
                    delete[] str;
                }
            }
        };

        // Compile vs
        GLint compiled;
        GL( glCompileShader( vs ) );
        GL( glGetShaderiv( vs, GL_COMPILE_STATUS, &compiled ) );
        if( !compiled )
        {
            WriteLogF( _FUNC_, " - Vertex shader not compiled, effect '%s'.\n", fname, str );
            ShaderInfo::Log( "Vertex shader", vs );
            GL( glDeleteShader( vs ) );
            GL( glDeleteShader( fs ) );
            return false;
        }

        // Compile fs
        GL( glCompileShader( fs ) );
        GL( glGetShaderiv( fs, GL_COMPILE_STATUS, &compiled ) );
        if( !compiled )
        {
            WriteLogF( _FUNC_, " - Fragment shader not compiled, effect '%s'.\n", fname );
            ShaderInfo::Log( "Fragment shader", fs );
            GL( glDeleteShader( vs ) );
            GL( glDeleteShader( fs ) );
            return false;
        }

        // Make program
        GL( program = glCreateProgram() );
        GL( glAttachShader( program, vs ) );
        GL( glAttachShader( program, fs ) );

        if( use_in_2d )
        {
            GL( glBindAttribLocation( program, 0, "InPosition" ) );
            GL( glBindAttribLocation( program, 1, "InColor" ) );
            GL( glBindAttribLocation( program, 2, "InTexCoord" ) );
            #ifndef DISABLE_EGG
            GL( glBindAttribLocation( program, 3, "InTexEggCoord" ) );
            #endif
        }
        else
        {
            GL( glBindAttribLocation( program, 0, "InPosition" ) );
            GL( glBindAttribLocation( program, 1, "InNormal" ) );
            GL( glBindAttribLocation( program, 2, "InTexCoord" ) );
            GL( glBindAttribLocation( program, 3, "InTexCoordBase" ) );
            GL( glBindAttribLocation( program, 4, "InTangent" ) );
            GL( glBindAttribLocation( program, 5, "InBitangent" ) );
            GL( glBindAttribLocation( program, 6, "InBlendWeights" ) );
            GL( glBindAttribLocation( program, 7, "InBlendIndices" ) );
        }

        if( GL_HAS( get_program_binary ) )
            GL( glProgramParameteri( program, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE ) );

        GL( glLinkProgram( program ) );
        GLint linked;
        GL( glGetProgramiv( program, GL_LINK_STATUS, &linked ) );
        if( !linked )
        {
            WriteLogF( _FUNC_, " - Failed to link shader program, effect '%s'.\n", fname );
            ShaderInfo::LogProgram( program );
            GL( glDetachShader( program, vs ) );
            GL( glDetachShader( program, fs ) );
            GL( glDeleteShader( vs ) );
            GL( glDeleteShader( fs ) );
            GL( glDeleteProgram( program ) );
            return false;
        }

        // Save in binary
        if( GL_HAS( get_program_binary ) )
        {
            GLsizei  buf_size;
            GL( glGetProgramiv( program, GL_PROGRAM_BINARY_LENGTH, &buf_size ) );
            GLsizei  length = 0;
            GLenum   format = 0;
            UCharVec buf;
            buf.resize( buf_size );
            GL( glGetProgramBinary( program, buf_size, &length, &format, &buf[ 0 ] ) );
            file_binary.SetBEUInt( FONLINE_VERSION );
            file_binary.SetBEUInt( format );
            file_binary.SetBEUInt( length );
            file_binary.SetData( &buf[ 0 ], length );
            if( !file_binary.SaveOutBufToFile( binary_fname, PT_CACHE ) )
                WriteLogF( _FUNC_, " - Can't save effect '%s' pass %u in binary '%s'.\n", fname, pass, binary_fname );
        }
    }

    // Bind data
    GL( effect_pass.ProjectionMatrix = glGetUniformLocation( program, "ProjectionMatrix" ) );
    GL( effect_pass.ZoomFactor = glGetUniformLocation( program, "ZoomFactor" ) );
    GL( effect_pass.ColorMap = glGetUniformLocation( program, "ColorMap" ) );
    GL( effect_pass.ColorMapSize = glGetUniformLocation( program, "ColorMapSize" ) );
    GL( effect_pass.ColorMapSamples = glGetUniformLocation( program, "ColorMapSamples" ) );
    GL( effect_pass.EggMap = glGetUniformLocation( program, "EggMap" ) );
    GL( effect_pass.EggMapSize = glGetUniformLocation( program, "EggMapSize" ) );
    GL( effect_pass.SpriteBorder = glGetUniformLocation( program, "SpriteBorder" ) );
    GL( effect_pass.GroundPosition = glGetUniformLocation( program, "GroundPosition" ) );
    GL( effect_pass.LightColor = glGetUniformLocation( program, "LightColor" ) );
    GL( effect_pass.WorldMatrices = glGetUniformLocation( program, "WorldMatrices" ) );

    GL( effect_pass.Time = glGetUniformLocation( program, "Time" ) );
    effect_pass.TimeCurrent = 0.0f;
    effect_pass.TimeLastTick = Timer::AccurateTick();
    GL( effect_pass.TimeGame = glGetUniformLocation( program, "TimeGame" ) );
    effect_pass.TimeGameCurrent = 0.0f;
    effect_pass.TimeGameLastTick = Timer::AccurateTick();
    effect_pass.IsTime = ( effect_pass.Time != -1 || effect_pass.TimeGame != -1 );
    GL( effect_pass.Random1 = glGetUniformLocation( program, "Random1Effect" ) );
    GL( effect_pass.Random2 = glGetUniformLocation( program, "Random2Effect" ) );
    GL( effect_pass.Random3 = glGetUniformLocation( program, "Random3Effect" ) );
    GL( effect_pass.Random4 = glGetUniformLocation( program, "Random4Effect" ) );
    effect_pass.IsRandom = ( effect_pass.Random1 != -1 || effect_pass.Random2 != -1 || effect_pass.Random3 != -1 || effect_pass.Random4 != -1 );
    effect_pass.IsTextures = false;
    for( int i = 0; i < EFFECT_TEXTURES; i++ )
    {
        char tex_name[ 32 ];
        Str::Format( tex_name, "Texture%d", i );
        GL( effect_pass.Textures[ i ] = glGetUniformLocation( program, tex_name ) );
        if( effect_pass.Textures[ i ] != -1 )
        {
            effect_pass.IsTextures = true;
            Str::Format( tex_name, "Texture%dSize", i );
            GL( effect_pass.TexturesSize[ i ] = glGetUniformLocation( program, tex_name ) );
            Str::Format( tex_name, "Texture%dAtlasOffset", i );
            GL( effect_pass.TexturesAtlasOffset[ i ] = glGetUniformLocation( program, tex_name ) );
        }
    }
    effect_pass.IsScriptValues = false;
    for( int i = 0; i < EFFECT_SCRIPT_VALUES; i++ )
    {
        char val_name[ 32 ];
        Str::Format( val_name, "EffectValue%d", i );
        GL( effect_pass.ScriptValues[ i ] = glGetUniformLocation( program, val_name ) );
        if( effect_pass.ScriptValues[ i ] != -1 )
            effect_pass.IsScriptValues = true;
    }
    GL( effect_pass.AnimPosProc = glGetUniformLocation( program, "AnimPosProc" ) );
    GL( effect_pass.AnimPosTime = glGetUniformLocation( program, "AnimPosTime" ) );
    effect_pass.IsAnimPos = ( effect_pass.AnimPosProc != -1 || effect_pass.AnimPosTime != -1 );
    effect_pass.IsNeedProcess = ( effect_pass.IsTime || effect_pass.IsRandom || effect_pass.IsTextures ||
                                  effect_pass.IsScriptValues || effect_pass.IsAnimPos || effect_pass.IsChangeStates );

    // Set defaults
    if( defaults )
    {
        GL( glUseProgram( program ) );
        for( uint d = 0; d < defaults_count; d++ )
        {
            EffectDefault& def = defaults[ d ];

            GLint          location = -1;
            GL( location = glGetUniformLocation( program, def.Name ) );
            if( !IS_EFFECT_VALUE( location ) )
                continue;

            switch( def.Type )
            {
            case EffectDefault::String:
                break;
            case EffectDefault::Float:
                GL( glUniform1fv( location, def.Size / sizeof( float ), (GLfloat*) def.Data ) );
                break;
            case EffectDefault::Int:
                GL( glUniform1iv( location, def.Size / sizeof( int ), (GLint*) def.Data ) );
                break;
            default:
                break;
            }
        }
        GL( glUseProgram( 0 ) );
    }

    effect_pass.Program = program;
    effect->Passes.push_back( effect_pass );
    return true;
}

void GraphicLoader::EffectProcessVariables( EffectPass& effect_pass, bool start,  float anim_proc /* = 0.0f */, float anim_time /* = 0.0f */, MeshTexture** textures /* = NULL */ )
{
    // Process effect
    if( start )
    {
        if( effect_pass.IsTime )
        {
            double tick = Timer::AccurateTick();
            if( IS_EFFECT_VALUE( effect_pass.Time ) )
            {
                effect_pass.TimeCurrent += (float) ( tick - effect_pass.TimeLastTick );
                effect_pass.TimeLastTick = tick;
                if( effect_pass.TimeCurrent >= 120.0f )
                    effect_pass.TimeCurrent = fmod( effect_pass.TimeCurrent, 120.0f );

                SET_EFFECT_VALUE( effect, effect_pass.Time, effect_pass.TimeCurrent );
            }
            if( IS_EFFECT_VALUE( effect_pass.TimeGame ) )
            {
                if( !Timer::IsGamePaused() )
                {
                    effect_pass.TimeGameCurrent += (float) ( tick - effect_pass.TimeGameLastTick ) / 1000.0f;
                    effect_pass.TimeGameLastTick = tick;
                    if( effect_pass.TimeGameCurrent >= 120.0f )
                        effect_pass.TimeGameCurrent = fmod( effect_pass.TimeGameCurrent, 120.0f );
                }
                else
                {
                    effect_pass.TimeGameLastTick = tick;
                }

                SET_EFFECT_VALUE( effect, effect_pass.TimeGame, effect_pass.TimeGameCurrent );
            }
        }

        if( effect_pass.IsRandom )
        {
            if( IS_EFFECT_VALUE( effect_pass.Random1 ) )
                SET_EFFECT_VALUE( effect, effect_pass.Random1, (float) ( (double) Random( 0, 2000000000 ) / 2000000000.0 ) );
            if( IS_EFFECT_VALUE( effect_pass.Random2 ) )
                SET_EFFECT_VALUE( effect, effect_pass.Random2, (float) ( (double) Random( 0, 2000000000 ) / 2000000000.0 ) );
            if( IS_EFFECT_VALUE( effect_pass.Random3 ) )
                SET_EFFECT_VALUE( effect, effect_pass.Random3, (float) ( (double) Random( 0, 2000000000 ) / 2000000000.0 ) );
            if( IS_EFFECT_VALUE( effect_pass.Random4 ) )
                SET_EFFECT_VALUE( effect, effect_pass.Random4, (float) ( (double) Random( 0, 2000000000 ) / 2000000000.0 ) );
        }

        if( effect_pass.IsTextures )
        {
            for( int i = 0; i < EFFECT_TEXTURES; i++ )
            {
                if( IS_EFFECT_VALUE( effect_pass.Textures[ i ] ) )
                {
                    GLuint id = ( textures && textures[ i ] ? textures[ i ]->Id : 0 );
                    GL( glActiveTexture( GL_TEXTURE2 + i ) );
                    GL( glBindTexture( GL_TEXTURE_2D, id ) );
                    GL( glActiveTexture( GL_TEXTURE0 ) );
                    GL( glUniform1i( effect_pass.Textures[ i ], 2 + i ) );
                    if( effect_pass.TexturesSize[ i ] != -1 && textures && textures[ i ] )
                        GL( glUniform4fv( effect_pass.TexturesSize[ i ], 1, textures[ i ]->SizeData ) );
                    if( effect_pass.TexturesAtlasOffset[ i ] != -1 && textures && textures[ i ] )
                        GL( glUniform4fv( effect_pass.TexturesAtlasOffset[ i ], 1, textures[ i ]->AtlasOffsetData ) );
                }
            }
        }

        if( effect_pass.IsScriptValues )
        {
            for( int i = 0; i < EFFECT_SCRIPT_VALUES; i++ )
                if( IS_EFFECT_VALUE( effect_pass.ScriptValues[ i ] ) )
                    SET_EFFECT_VALUE( effect, effect_pass.ScriptValues[ i ], GameOpt.EffectValues[ i ] );
        }

        if( effect_pass.IsAnimPos )
        {
            if( IS_EFFECT_VALUE( effect_pass.AnimPosProc ) )
                SET_EFFECT_VALUE( effect, effect_pass.AnimPosProc, anim_proc );
            if( IS_EFFECT_VALUE( effect_pass.AnimPosTime ) )
                SET_EFFECT_VALUE( effect, effect_pass.AnimPosTime, anim_time );
        }

        if( effect_pass.IsChangeStates )
        {
            if( effect_pass.BlendFuncParam1 )
                GL( glBlendFunc( effect_pass.BlendFuncParam1, effect_pass.BlendFuncParam2 ) );
            if( effect_pass.BlendEquation )
                GL( glBlendEquation( effect_pass.BlendEquation ) );
        }
    }
    // Finish processing
    else
    {
        if( effect_pass.IsChangeStates )
        {
            if( effect_pass.BlendFuncParam1 )
                GL( glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA ) );
            if( effect_pass.BlendEquation )
                GL( glBlendEquation( GL_FUNC_ADD ) );
        }
    }
}

/*
   Todo:
        if(Name) delete Name;
        if(Effect)
        {
                if(EffectParams) Effect->DeleteParameterBlock(EffectParams);
                Effect->Release();
                Effect=NULL;
        }
 */

uint    Effect::MaxBones;

Effect* Effect::Contour, * Effect::ContourDefault;
Effect* Effect::Generic, * Effect::GenericDefault;
Effect* Effect::Critter, * Effect::CritterDefault;
Effect* Effect::Tile, * Effect::TileDefault;
Effect* Effect::Roof, * Effect::RoofDefault;
Effect* Effect::Rain, * Effect::RainDefault;
Effect* Effect::Iface, * Effect::IfaceDefault;
Effect* Effect::Primitive, * Effect::PrimitiveDefault;
Effect* Effect::Light, * Effect::LightDefault;
Effect* Effect::Fog, * Effect::FogDefault;
Effect* Effect::FlushRenderTarget, * Effect::FlushRenderTargetDefault;
Effect* Effect::FlushRenderTargetMS, * Effect::FlushRenderTargetMSDefault;
Effect* Effect::FlushPrimitive, * Effect::FlushPrimitiveDefault;
Effect* Effect::FlushMap, * Effect::FlushMapDefault;
Effect* Effect::FlushLight, * Effect::FlushLightDefault;
Effect* Effect::FlushFog, * Effect::FlushFogDefault;
Effect* Effect::Font, * Effect::FontDefault;
Effect* Effect::Skinned3d, * Effect::Skinned3dDefault;

#define LOAD_EFFECT( effect_handle, effect_name, use_in_2d, defines )         \
    effect_handle ## Default = LoadEffect( effect_name, use_in_2d, defines ); \
    if( effect_handle ## Default )                                            \
        effect_handle = new Effect( *effect_handle ## Default );              \
    else                                                                      \
        effect_errors++

bool GraphicLoader::LoadMinimalEffects()
{
    uint effect_errors = 0;
    LOAD_EFFECT( Effect::Font, "Font_Default", true, nullptr );
    LOAD_EFFECT( Effect::FlushRenderTarget, "Flush_RenderTarget", true, nullptr );
    if( effect_errors > 0 )
    {
        WriteLog( "Minimal effects not loaded.\n" );
        return false;
    }
    return true;
}

bool GraphicLoader::LoadDefaultEffects()
{
    // Default effects
    uint effect_errors = 0;
    #ifndef DISABLE_EGG
    LOAD_EFFECT( Effect::Generic, "2D_Default", true, nullptr );
    LOAD_EFFECT( Effect::Critter, "2D_Default", true, nullptr );
    LOAD_EFFECT( Effect::Roof, "2D_Default", true, nullptr );
    #else
    LOAD_EFFECT( Effect::Generic, "2D_WithoutEgg", true, nullptr );
    LOAD_EFFECT( Effect::Critter, "2D_WithoutEgg", true, nullptr );
    LOAD_EFFECT( Effect::Roof, "2D_WithoutEgg", true, nullptr );
    #endif
    LOAD_EFFECT( Effect::Rain, "2D_WithoutEgg", true, nullptr );
    LOAD_EFFECT( Effect::Iface, "Interface_Default", true, nullptr );
    LOAD_EFFECT( Effect::Primitive, "Primitive_Default", true, nullptr );
    LOAD_EFFECT( Effect::Light, "Primitive_Light", true, nullptr );
    LOAD_EFFECT( Effect::Fog, "Primitive_Fog", true, nullptr );
    LOAD_EFFECT( Effect::Font, "Font_Default", true, nullptr );
    LOAD_EFFECT( Effect::Contour, "Contour_Default", true, nullptr );
    LOAD_EFFECT( Effect::Tile, "2D_WithoutEgg", true, nullptr );
    LOAD_EFFECT( Effect::FlushRenderTarget, "Flush_RenderTarget", true, nullptr );
    LOAD_EFFECT( Effect::FlushPrimitive, "Flush_Primitive", true, nullptr );
    LOAD_EFFECT( Effect::FlushMap, "Flush_Map", true, nullptr );
    LOAD_EFFECT( Effect::FlushLight, "Flush_Light", true, nullptr );
    LOAD_EFFECT( Effect::FlushFog, "Flush_Fog", true, nullptr );
    if( effect_errors > 0 )
    {
        WriteLog( "Default effects not loaded.\n" );
        return false;
    }
    return true;
}

bool GraphicLoader::Load3dEffects()
{
    uint effect_errors = 0;
    LOAD_EFFECT( Effect::Skinned3d, "3D_Skinned", false, nullptr );
    if( effect_errors > 0 )
    {
        WriteLog( "3D effects not loaded.\n" );
        return false;
    }
    return true;
}

/************************************************************************/
/*                                                                      */
/************************************************************************/

uchar* GraphicLoader::LoadPNG( const uchar* data, uint data_size, uint& result_width, uint& result_height )
{
    result_width = *(uint*) data;
    result_height = *(uint*) ( data + 4 );
    if( result_width * result_height * 4 != data_size - 8 )
        return nullptr;
    uchar* result = new uchar[ result_width * result_height * 4 ];
    memcpy( result, data + 8, result_width * result_height * 4 );
    return result;
}

void GraphicLoader::SavePNG( const char* fname, uchar* data, uint width, uint height )
{
    // Initialize stuff
    png_structp png_ptr = png_create_write_struct( PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr );
    if( !png_ptr )
        return;
    png_infop info_ptr = png_create_info_struct( png_ptr );
    if( !info_ptr )
        return;
    if( setjmp( png_jmpbuf( png_ptr ) ) )
        return;

    static UCharVec result_png;
    struct PNGWriter
    {
        static void Write( png_structp png_ptr, png_bytep png_data, png_size_t length )
        {
            UNUSED_VARIABLE( png_ptr );
            for( png_size_t i = 0; i < length; i++ )
                result_png.push_back( png_data[ i ] );
        }
        static void Flush( png_structp png_ptr )
        {
            UNUSED_VARIABLE( png_ptr );
        }
    };
    result_png.clear();
    png_set_write_fn( png_ptr, nullptr, &PNGWriter::Write, &PNGWriter::Flush );

    // Write header
    if( setjmp( png_jmpbuf( png_ptr ) ) )
        return;
    png_byte color_type = PNG_COLOR_TYPE_RGB_ALPHA;
    png_byte bit_depth = 8;
    png_set_IHDR( png_ptr, info_ptr, width, height, bit_depth, color_type, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE );
    png_write_info( png_ptr, info_ptr );

    // Write pointers
    uchar** row_pointers = new uchar*[ height ];
    for( uint y = 0; y < height; y++ )
        row_pointers[ y ] = &data[ y * width * 4 ];

    // Write bytes
    if( setjmp( png_jmpbuf( png_ptr ) ) )
        return;
    png_write_image( png_ptr, row_pointers );

    // End write
    if( setjmp( png_jmpbuf( png_ptr ) ) )
        return;
    png_write_end( png_ptr, nullptr );

    // Clean up
    delete[] row_pointers;

    // Write to disk
    FileManager fm;
    fm.SetData( &result_png[ 0 ], (uint) result_png.size() );
    fm.SaveOutBufToFile( fname, PT_ROOT );
}

uchar* GraphicLoader::LoadTGA( const uchar* data, uint data_size, uint& result_width, uint& result_height )
{
    result_width = *(uint*) data;
    result_height = *(uint*) ( data + 4 );
    if( result_width * result_height * 4 != data_size - 8 )
        return nullptr;
    uchar* result = new uchar[ result_width * result_height * 4 ];
    memcpy( result, data + 8, result_width * result_height * 4 );
    return result;
}
