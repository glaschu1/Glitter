// Local Headers
#include "glitter.hpp"

// Std. Includes
#include <string>



// System Headers
#include <glad/glad.h>
#include <GLFW/glfw3.h>

// GL includes
#include "Shader.h"
#include "Camera.h"
#include "Model.h"
#include "Texture.h"
#include "skymap.h"
// Standard Headers
#include <cstdio>
#include <cstdlib>

#include <irrKlang.h>
#pragma comment(lib, "irrKlang.lib") // link with irrKlang.dll
// Properties
//const GLuint WIDTH = 800, HEIGHT = 600;
int SCREEN_WIDTH, SCREEN_HEIGHT;

// Function prototypes
void KeyCallback(GLFWwindow *window, int key, int scancode, int action, int mode);
void MouseCallback(GLFWwindow *window, double xPos, double yPos);
void DoMovement();
void LoadModel(Shader shader, glm::mat4 projection, Model Model, Camera camera, glm::vec3 Pos);
void LoadFloor(Shader shader, glm::mat4 projection, Model Model, Camera camera);
void LoadTarget(Shader shader, glm::mat4 projection, Model Model, Camera camera, glm::vec3 Pos);
void LoadBuilding(Shader shader, glm::mat4 projection, Model Model, Camera camera, glm::vec3 Pos);
GLuint loadTexture(GLchar const * path);
GLuint loadCubemap(std::vector<std::string> faces);
bool FirstCam = true;
int state = 0;

void ScreenPosToWorldRay(
	int mouseX, int mouseY,             // Mouse position, in pixels, from bottom-left corner of the window
	int screenWidth, int screenHeight,  // Window size, in pixels
	glm::mat4 ViewMatrix,               // Camera position and orientation
	glm::mat4 ProjectionMatrix,         // Camera parameters (ratio, field of view, near and far planes)
	glm::vec3& out_origin,              // Ouput : Origin of the ray. /!\ Starts at the near plane, so if you want the ray to start at the camera's position instead, ignore this.
	glm::vec3& out_direction            // Ouput : Direction, in world space, of the ray that goes "through" the mouse.
) {

	// The ray Start and End positions, in Normalized Device Coordinates 
	glm::vec4 lRayStart_NDC(
		((float)mouseX / (float)screenWidth - 0.5f) * 2.0f, // [0,1024] -> [-1,1]
		((float)mouseY / (float)screenHeight - 0.5f) * 2.0f, // [0, 768] -> [-1,1]
		-1.0, // The near plane maps to Z=-1 in Normalized Device Coordinates
		1.0f
	);
	glm::vec4 lRayEnd_NDC(
		((float)mouseX / (float)screenWidth - 0.5f) * 2.0f,
		((float)mouseY / (float)screenHeight - 0.5f) * 2.0f,
		0.0,
		1.0f
	);


	// The Projection matrix goes from Camera Space to NDC.
	// So inverse(ProjectionMatrix) goes from NDC to Camera Space.
	glm::mat4 InverseProjectionMatrix = glm::inverse(ProjectionMatrix);

	// The View Matrix goes from World Space to Camera Space.
	// So inverse(ViewMatrix) goes from Camera Space to World Space.
	glm::mat4 InverseViewMatrix = glm::inverse(ViewMatrix);

	glm::vec4 lRayStart_camera = InverseProjectionMatrix * lRayStart_NDC;    lRayStart_camera /= lRayStart_camera.w;
	glm::vec4 lRayStart_world = InverseViewMatrix       * lRayStart_camera; lRayStart_world /= lRayStart_world.w;
	glm::vec4 lRayEnd_camera = InverseProjectionMatrix * lRayEnd_NDC;      lRayEnd_camera /= lRayEnd_camera.w;
	glm::vec4 lRayEnd_world = InverseViewMatrix       * lRayEnd_camera;   lRayEnd_world /= lRayEnd_world.w;




	glm::vec3 lRayDir_world(lRayEnd_world - lRayStart_world);
	lRayDir_world = glm::normalize(lRayDir_world);


	out_origin = glm::vec3(lRayStart_world);
	out_direction = glm::normalize(lRayDir_world);
}
bool TestRayOBBIntersection(
	glm::vec3 ray_origin,        // Ray origin, in world space
	glm::vec3 ray_direction,     // Ray direction (NOT target position!), in world space. Must be normalize()'d.
	glm::vec3 aabb_min,          // Minimum X,Y,Z coords of the mesh when not transformed at all.
	glm::vec3 aabb_max,          // Maximum X,Y,Z coords. Often aabb_min*-1 if your mesh is centered, but it's not always the case.
	glm::mat4 ModelMatrix,       // Transformation applied to the mesh (which will thus be also applied to its bounding box)
	float& intersection_distance // Output : distance between ray_origin and the intersection with the OBB
) {

	// Intersection method from Real-Time Rendering and Essential Mathematics for Games

	float tMin = 0.0f;
	float tMax = 100.0f;

	glm::vec3 OBBposition_worldspace(ModelMatrix[3].x, ModelMatrix[3].y, ModelMatrix[3].z);

	glm::vec3 delta = OBBposition_worldspace - ray_origin;

	// Test intersection with the 2 planes perpendicular to the OBB's X axis
	{
		glm::vec3 xaxis(ModelMatrix[0].x, ModelMatrix[0].y, ModelMatrix[0].z);
		float e = glm::dot(xaxis, delta);
		float f = glm::dot(ray_direction, xaxis);

		if (fabs(f) > 0.001f) { // Standard case

			float t1 = (e + aabb_min.x) / f; // Intersection with the "left" plane
			float t2 = (e + aabb_max.x) / f; // Intersection with the "right" plane
											 // t1 and t2 now contain distances betwen ray origin and ray-plane intersections

											 // We want t1 to represent the nearest intersection, 
											 // so if it's not the case, invert t1 and t2
			if (t1>t2) {
				float w = t1; t1 = t2; t2 = w; // swap t1 and t2
			}

			// tMax is the nearest "far" intersection (amongst the X,Y and Z planes pairs)
			if (t2 < tMax)
				tMax = t2;
			// tMin is the farthest "near" intersection (amongst the X,Y and Z planes pairs)
			if (t1 > tMin)
				tMin = t1;

			// And here's the trick :
			// If "far" is closer than "near", then there is NO intersection.
			// See the images in the tutorials for the visual explanation.
			if (tMax < tMin)
				return false;

		}
		else { // Rare case : the ray is almost parallel to the planes, so they don't have any "intersection"
			if (-e + aabb_min.x > 0.0f || -e + aabb_max.x < 0.0f)
				return false;
		}
	}


	// Test intersection with the 2 planes perpendicular to the OBB's Y axis
	// Exactly the same thing than above.
	{
		glm::vec3 yaxis(ModelMatrix[1].x, ModelMatrix[1].y, ModelMatrix[1].z);
		float e = glm::dot(yaxis, delta);
		float f = glm::dot(ray_direction, yaxis);

		if (fabs(f) > 0.001f) {

			float t1 = (e + aabb_min.y) / f;
			float t2 = (e + aabb_max.y) / f;

			if (t1>t2) { float w = t1; t1 = t2; t2 = w; }

			if (t2 < tMax)
				tMax = t2;
			if (t1 > tMin)
				tMin = t1;
			if (tMin > tMax)
				return false;

		}
		else {
			if (-e + aabb_min.y > 0.0f || -e + aabb_max.y < 0.0f)
				return false;
		}
	}


	// Test intersection with the 2 planes perpendicular to the OBB's Z axis
	// Exactly the same thing than above.
	{
		glm::vec3 zaxis(ModelMatrix[2].x, ModelMatrix[2].y, ModelMatrix[2].z);
		float e = glm::dot(zaxis, delta);
		float f = glm::dot(ray_direction, zaxis);

		if (fabs(f) > 0.001f) {

			float t1 = (e + aabb_min.z) / f;
			float t2 = (e + aabb_max.z) / f;

			if (t1>t2) { float w = t1; t1 = t2; t2 = w; }

			if (t2 < tMax)
				tMax = t2;
			if (t1 > tMin)
				tMin = t1;
			if (tMin > tMax)
				return false;

		}
		else {
			if (-e + aabb_min.z > 0.0f || -e + aabb_max.z < 0.0f)
				return false;
		}
	}

	intersection_distance = tMin;
	return true;

}
Skymap Skybox;
// Camera
Camera camera(glm::vec3(0.0f, 0.0f, 3.0f));
bool keys[1024];
GLfloat lastX = mWidth/2.0, lastY = mHeight/2.0;
bool firstMouse = true;

glm::vec3 TestPos = glm::vec3(50.0f, -2.0f, 0.0f);
glm::vec3 PlayerPos = glm::vec3(0.0f, -3.5f, 0.0f);

GLfloat deltaTime = 0.0f;
GLfloat lastFrame = 0.0f;


bool Reverse = false;
bool Right = false;
bool Hit0 = false, Hit1 = false, Hit2 = false, Hit3 = false, Hit4 = false, Hit5 = false, Hit6 = false;
bool OBJHit[6] = { false, false, false, false, false,false };

int main(int argc, char * argv[]) {

	// Load GLFW and Create a Window
	glfwInit();
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
	glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);
	auto mWindow = glfwCreateWindow(mWidth, mHeight, "OpenGL", nullptr, nullptr);

	// Check for Valid Context
	if (mWindow == nullptr) {
		fprintf(stderr, "Failed to Create OpenGL Context");
		return EXIT_FAILURE;
	}

	// Create Context and Load OpenGL Functions
	
	glfwGetFramebufferSize(mWindow, &SCREEN_WIDTH, &SCREEN_HEIGHT);
	glfwMakeContextCurrent(mWindow);
	gladLoadGL();
	fprintf(stderr, "OpenGL %s\n", glGetString(GL_VERSION));


	// Set the required callback functions
	glfwSetKeyCallback(mWindow, KeyCallback);
	glfwSetCursorPosCallback(mWindow, MouseCallback);
	// Options, removes the mouse cursor for a more immersive experience
	glfwSetInputMode(mWindow, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

	// start the sound engine with default parameters
	irrklang::ISoundEngine* engine = irrklang::createIrrKlangDevice();
	irrklang::ISoundSource* shootSound = engine->addSoundSourceFromFile("./res/Wind.ogg");
	irrklang::ISound* snd = engine->play2D(shootSound, true);
	if (!engine)
		return 0; // error starting up the engine
	engine->play2D(shootSound, true);
	
	//Shader ourShader("./res/shaders/coordinate_systems.vs", "./res/shaders/coordinate_systems.frag");

	// Build and compile our shader program
	Shader BoxShader("./res/shaders/texture.vs", "./res/shaders/texture.frag");
	Shader BoxShader2("./res/shaders/texture.vs", "./res/shaders/texture.frag");
	Shader BoxShader3("./res/shaders/texture.vs", "./res/shaders/texture.frag");
	// Set up vertex data (and buffer(s)) and attribute pointers
	GLfloat vertices[] = {
		// Positions          // Colors           // Texture Coords
		1.0f,  1.0f, 0.0f,   1.0f, 0.0f, 0.0f,   1.0f, 1.0f, // Top Right
		1.0f, -1.0f, 0.0f,   0.0f, 1.0f, 0.0f,   1.0f, 0.0f, // Bottom Right
		-1.0f, -1.0f, 0.0f,   0.0f, 0.0f, 1.0f,   0.0f, 0.0f, // Bottom Left
		-1.0f,  1.0f, 0.0f,   1.0f, 1.0f, 0.0f,   0.0f, 1.0f  // Top Left 
	};
	GLuint indices[] = {  // Note that we start from 0!
		0, 1, 3, // First Triangle
		1, 2, 3  // Second Triangle
	};
	GLuint VBO, VAO, EBO;
	glGenVertexArrays(1, &VAO);
	glGenBuffers(1, &VBO);
	glGenBuffers(1, &EBO);

	glBindVertexArray(VAO);

	glBindBuffer(GL_ARRAY_BUFFER, VBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

	// Position attribute
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(GLfloat), (GLvoid*)0);
	glEnableVertexAttribArray(0);
	// Color attribute
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(GLfloat), (GLvoid*)(3 * sizeof(GLfloat)));
	glEnableVertexAttribArray(1);
	// TexCoord attribute
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(GLfloat), (GLvoid*)(6 * sizeof(GLfloat)));
	glEnableVertexAttribArray(2);

	glBindVertexArray(0); // Unbind VAO


						  // Load and create a texture 
	GLuint texture1;
	GLuint texture2;
	GLuint texture3;
	// ====================
	// Texture 1
	// ====================
	glGenTextures(1, &texture1);
	glBindTexture(GL_TEXTURE_2D, texture1); // All upcoming GL_TEXTURE_2D operations now have effect on our texture object
											// Set our texture parameters
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);	// Set texture wrapping to GL_REPEAT
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	// Set texture filtering
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	// Load, create texture and generate mipmaps
	int width, height;
	unsigned char* image = stbi_load("./res/textures/Start.jpg", &width, &height, 0, STBI_rgb);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, image);
	glGenerateMipmap(GL_TEXTURE_2D);
	stbi_image_free(image);
	glBindTexture(GL_TEXTURE_2D, 0); // Unbind texture when done, so we won't accidentily mess up our texture.
									 // ===================
									 // Texture 2
									 // ===================
	glGenTextures(1, &texture2);
	glBindTexture(GL_TEXTURE_2D, texture2);
	// Set our texture parameters
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	// Set texture filtering
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	// Load, create texture and generate mipmaps
	image = stbi_load("./res/textures/GoodEnd.jpg", &width, &height, 0, STBI_rgb);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, image);
	glGenerateMipmap(GL_TEXTURE_2D);
	stbi_image_free(image);
	glBindTexture(GL_TEXTURE_2D, 0);
	glBindVertexArray(0); // Unbind VAO
						  // ===================
						  // Texture 3
						  // ===================
	glGenTextures(1, &texture3);
	glBindTexture(GL_TEXTURE_2D, texture3);
	// Set our texture parameters
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	// Set texture filtering
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	// Load, create texture and generate mipmaps
	image = stbi_load("./res/textures/BadEnd.jpg", &width, &height, 0, STBI_rgb);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, image);
	glGenerateMipmap(GL_TEXTURE_2D);
	stbi_image_free(image);
	glBindTexture(GL_TEXTURE_2D, 0);
	glBindVertexArray(0); // Unbind VAO

						  // Define the viewport dimensions
	glViewport(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

	// OpenGL options
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);

	// Setup and compile our shaders
	//Shader shader("./res/shaders/cubemaps.vs", "./res/shaders/cubemaps.frag");
	Shader skyboxShader("./res/shaders/skybox.vs", "./res/shaders/skybox.frag");

	Shader Modelshader("./res/shaders/shader.vs", "./res/shaders/shader.frag");
	// Load models
	Model ourModel("./res/objects/nanosuit/nanosuit.obj");
	Model MountModel("./res/objects/Mount/terrain 1 low polly.obj");
	Model TargetModel("./res/objects/cyborg/cyborg.obj");
	Model TargetBul("./res/objects/Wooden-Watch-Tower/wooden watch tower2.obj");


	 //Setup skybox VAO
	GLuint skyboxVAO = 0, skyboxVBO = 0;
	Skybox.CreateBuffers(skyboxVAO, skyboxVBO);
	skyboxVAO = Skybox.GetVAO();
	skyboxVBO = Skybox.GetVBO();

	// Cubemap (Skybox)
	std::vector<std::string> faces;
	faces.push_back("./res/textures/skybox/right.jpg");
	faces.push_back("./res/textures/skybox/left.jpg");
	faces.push_back("./res/textures/skybox/top.jpg");
	faces.push_back("./res/textures/skybox/bottom.jpg");
	faces.push_back("./res/textures/skybox/back.jpg");
	faces.push_back("./res/textures/skybox/front.jpg");
	GLuint skyboxTexture = loadCubemap(faces);

	//Picking
	glm::quat orientations;
	orientations = glm::normalize(glm::quat(glm::vec3(0.0f, 0.0f, 0.0f)));

	// Generate positions  
	std::vector<glm::vec3> positions(6);
	positions[0] = glm::vec3(-15.20, -2.00, -44.40);
	positions[1] = glm::vec3(-54.10, -2.00, -41.80);
	positions[2] = glm::vec3(-13.10, -2.00, 77.40);
	positions[3] = glm::vec3(60.31, -2.00, 105.90);
	positions[4] = glm::vec3(93.50, -2.00, -130.34);
	positions[5] = glm::vec3(-150.17, -2.00, -53.67);
	//positions[6] = glm::vec3(-150.17, -2.00, 26.99);
	std::vector<glm::vec3> positionsCopy(6);
	positionsCopy[0] = glm::vec3(-15.20, -2.00, -44.40);
	positionsCopy[1] = glm::vec3(-54.10, -2.00, -41.80);
	positionsCopy[2] = glm::vec3(-13.10, -2.00, 77.40);
	positionsCopy[3] = glm::vec3(60.31, -2.00, 105.90);
	positionsCopy[4] = glm::vec3(93.50, -2.00, -130.34);
	positionsCopy[5] = glm::vec3(-150.17, -2.00, -53.67);
	//positionsCopy[6] = glm::vec3(-150.17, -2.00, 26.99);


	// Rendering Loop
	while (glfwWindowShouldClose(mWindow) == false) {
		if (glfwGetKey(mWindow, GLFW_KEY_ESCAPE) == GLFW_PRESS)
			glfwSetWindowShouldClose(mWindow, true);

		// Set frame time
		GLfloat currentFrame = glfwGetTime();
		deltaTime = currentFrame - lastFrame;
		lastFrame = currentFrame;

		glfwPollEvents();
		DoMovement();

		//picking
		glm::vec3 out_origin1;
		glm::vec3 out_direction1;
		glm::mat4 view = camera.GetViewMatrix();
		glm::mat4 projection = glm::perspective(camera.GetZoom(), (float)mWidth / (float)mHeight, 0.1f, 1000.0f);


		// Camera controls
		if (glfwGetKey(mWindow, GLFW_KEY_P) == GLFW_PRESS) {
			if (FirstCam) {
				FirstCam = false;
				camera.setPos(glm::vec3(50.0f, 40.0f, 0.0f));
			}
			else { FirstCam = true; 
			camera.setPos(glm::vec3(PlayerPos.x, PlayerPos.y, PlayerPos.z));
			
			}
		}
		if (keys[GLFW_KEY_K]){ engine->setAllSoundsPaused(true); }
			//
		if (keys[GLFW_KEY_J]) { engine->setAllSoundsPaused(false); }
		//
		if (glfwGetKey(mWindow, GLFW_KEY_Q) == GLFW_PRESS) {
			state = 1;
			glfwSetTime(0.0);
		}


		// PICKING IS DONE HERE
		// (Instead of picking each frame if the mouse button is down, 
		// you should probably only check if the mouse button was just released)


		if (!Reverse) {
			for (int i = 0; i < 6; i++) {
				positions[i].z += 5.0f*deltaTime;
			}
		}
		else if (Reverse) {
			for (int i = 0; i < 6; i++) {
				positions[i].z -= 5.0f*deltaTime;
			}
		}

		if (positions[0].z>positionsCopy[0].z + 20.0f) {
			Reverse = true;
		}
		else if (positions[0].z<positionsCopy[0].z - 20.0f) {
			Reverse = false;
		}
		if (positions[0].x>positionsCopy[0].x + 10.0f) {
			Right = true;
		}
		else if (positions[0].x<positionsCopy[0].x - 10.0f) {
			Right = false;
		}


		if (glfwGetMouseButton(mWindow, GLFW_MOUSE_BUTTON_LEFT)) {

			glm::vec3 ray_origin;
			glm::vec3 ray_direction;
			ScreenPosToWorldRay(
				1024 / 2, 768 / 2,
				1024, 768,
				view,
				projection,
				ray_origin,
				ray_direction
			);


			for (int i = 0; i<6; i++) {

				float intersection_distance; // Output of TestRayOBBIntersection()
				glm::vec3 aabb_min(-1.0f, -3.0f, -1.0f);
				glm::vec3 aabb_max(1.0f, 3.0f, 1.0f);

				// The ModelMatrix transforms :
				// - the mesh to its desired position and orientation
				// - but also the AABB (defined with aabb_min and aabb_max) into an OBB
				glm::mat4 RotationMatrix = glm::toMat4(orientations);
				glm::mat4 TranslationMatrix = translate(glm::mat4(), positions[i]);
				glm::mat4 ModelMatrix = TranslationMatrix * RotationMatrix;


				if (TestRayOBBIntersection(
					ray_origin,
					ray_direction,
					aabb_min,
					aabb_max,
					ModelMatrix,
					intersection_distance)
					) {
					if (i == 0) {
						OBJHit[1] = true;
					}
					if (i == 1) {
						//Hit1 = true;
						OBJHit[2] = true;
					}
					if (i == 2) {
						//Hit2 = true;
						OBJHit[3] = true;
					}
					if (i == 3) {
						//Hit3 = true;
						OBJHit[4] = true;
					}
					if (i == 4) {
						//Hit4 = true;
						OBJHit[5] = true;
					}if (i == 5) {
						//Hit5 = true;
						OBJHit[6] = true;

					}

					printf("Collision ");
					break;
				}
			}


		}
		//double time = glfwGetTime();
		//printf("time  <%.2f> \n", time);

		// Background Fill Color
		glClearColor(0.25f, 0.25f, 0.25f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		if (state == 0) {
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, texture1);
			glUniform1i(glGetUniformLocation(BoxShader.Program, "ourTexture1"), 0);


			// Activate shader
			BoxShader.Use();

			// Draw container
			glBindVertexArray(VAO);
			glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
			glBindVertexArray(0);
		}


		if (state == 1) {
			glm::mat4 model;

			glDepthFunc(GL_LEQUAL);  // Change depth function so depth test passes when values are equal to depth buffer's content
			skyboxShader.Use();
			view = glm::mat4(glm::mat3(camera.GetViewMatrix()));	// Remove any translation component of the view matrix
			glUniformMatrix4fv(glGetUniformLocation(skyboxShader.Program, "view"), 1, GL_FALSE, glm::value_ptr(view));
			glUniformMatrix4fv(glGetUniformLocation(skyboxShader.Program, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
			// skybox cube
			glBindVertexArray(skyboxVAO);
			glBindTexture(GL_TEXTURE_CUBE_MAP, skyboxTexture);
			glDrawArrays(GL_TRIANGLES, 0, 36);
			glBindVertexArray(0);
			glDepthFunc(GL_LESS);  //Set depth function back to default
			for (int i = 0; i < 6; i++) {
				if(OBJHit[i+1] == false)
				LoadTarget(Modelshader, projection, TargetModel, camera, positions[i]);
			}
			LoadModel(Modelshader, projection, ourModel, camera, PlayerPos);
			LoadBuilding(Modelshader, projection, TargetBul, camera, TestPos);
			LoadFloor(Modelshader, projection, MountModel, camera);

			double time = glfwGetTime();
			if (time >= 60.0) {
				state = 3;
			}
			if (OBJHit[1]&& OBJHit[2] && OBJHit[3] && OBJHit[4] && OBJHit[5] && OBJHit[6]) {
				state = 2;
			}
		}

		if (state == 2) {
			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_2D, texture2);
			glUniform1i(glGetUniformLocation(BoxShader3.Program, "ourTexture2"), 1);


			// Activate shader
			BoxShader3.Use();

			// Draw container
			glBindVertexArray(VAO);
			glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
			glBindVertexArray(0);
		}
		if (state == 3) {
			glActiveTexture(GL_TEXTURE2);
			glBindTexture(GL_TEXTURE_2D, texture3);
			glUniform1i(glGetUniformLocation(BoxShader2.Program, "ourTexture1"), 2);


			// Activate shader
			BoxShader2.Use();

			// Draw container
			glBindVertexArray(VAO);
			glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
			glBindVertexArray(0);
		}

		// Flip Buffers and Draw
		glfwSwapBuffers(mWindow);
		
	}  
	glDeleteVertexArrays(1, &VAO);
	glDeleteBuffers(1, &VBO);

	glfwTerminate();
	return EXIT_SUCCESS;
}

GLuint loadCubemap(std::vector<std::string> faces)
{
	GLuint textureID;
	glGenTextures(1, &textureID);
	glActiveTexture(GL_TEXTURE0);

	int width, height;
	unsigned char* image;

	glBindTexture(GL_TEXTURE_CUBE_MAP, textureID);
	for (GLuint i = 0; i < faces.size(); i++)
	{
		image = stbi_load(faces[i].c_str(), &width, &height, 0, STBI_rgb);
		glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, image);
	}
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

	return textureID;
}


// This function loads a texture from file. Note: texture loading functions like these are usually 
// managed by a 'Resource Manager' that manages all resources (like textures, models, audio). 
// For learning purposes we'll just define it as a utility function.
GLuint loadTexture(GLchar const * path)
{
	//Generate texture ID and load texture data 
	GLuint textureID;
	glGenTextures(1, &textureID);
	int width, height;
	unsigned char* image = stbi_load(path, &width, &height, 0, STBI_rgb);
	// Assign texture to ID
	glBindTexture(GL_TEXTURE_2D, textureID);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, image);
	glGenerateMipmap(GL_TEXTURE_2D);

	// Parameters
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glBindTexture(GL_TEXTURE_2D, 0);
	stbi_image_free(image);
	return textureID;
}

void LoadModel(Shader shader, glm::mat4 projection, Model Model, Camera camera, glm::vec3 Pos) {
	glm::mat4 view = camera.GetViewMatrix();
	glm::mat4 model;
	glm::vec3 Viewtest = camera.GetView();
	GLfloat yaw = camera.GetYAW();

	shader.Use();
	glUniformMatrix4fv(glGetUniformLocation(shader.Program, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
	glUniformMatrix4fv(glGetUniformLocation(shader.Program, "view"), 1, GL_FALSE, glm::value_ptr(view));
	// Draw the loaded model//fix for not dependint on cam
	if (FirstCam) {
		model = glm::translate(model, glm::vec3(Viewtest.x, Viewtest.y - 3.5f, Viewtest.z));
	}
	else {
		model = glm::translate(model, glm::vec3(Pos.x, Pos.y, Pos.z));
	}

	// Translate it down a bit so it's at the center of the scene
	model = glm::scale(model, glm::vec3(0.2f, 0.2f, 0.2f));	// It's a bit too big for our scene, so scale it down
	model = glm::rotate(model, -glm::radians(yaw) - (-glm::radians(90.0f)), glm::vec3(0, 1, 0));
	glUniformMatrix4fv(glGetUniformLocation(shader.Program, "model"), 1, GL_FALSE, glm::value_ptr(model));
	Model.Draw(shader);

}
void LoadTarget(Shader shader, glm::mat4 projection, Model Model, Camera camera, glm::vec3 Pos) {
	glm::mat4 view = camera.GetViewMatrix();
	glm::mat4 model;
	shader.Use();
	glUniformMatrix4fv(glGetUniformLocation(shader.Program, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
	glUniformMatrix4fv(glGetUniformLocation(shader.Program, "view"), 1, GL_FALSE, glm::value_ptr(view));
	// Draw the loaded model//fix for not dependint on cam
	model = glm::translate(model, glm::vec3(Pos.x, Pos.y - 3.0f, Pos.z)); // Translate it down a bit so it's at the center of the scene
	model = glm::scale(model, glm::vec3(2.3f, 2.3f, 2.3f));	// It's a bit too big for our scene, so scale it down
															//model = glm::rotate(model, -glm::radians(yaw) - (-glm::radians(90.0f)), glm::vec3(0, 1, 0));
	glUniformMatrix4fv(glGetUniformLocation(shader.Program, "model"), 1, GL_FALSE, glm::value_ptr(model));
	Model.Draw(shader);

}
void LoadBuilding(Shader shader, glm::mat4 projection, Model Model, Camera camera, glm::vec3 Pos) {
	glm::mat4 view = camera.GetViewMatrix();
	glm::mat4 model;
	shader.Use();
	glUniformMatrix4fv(glGetUniformLocation(shader.Program, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
	glUniformMatrix4fv(glGetUniformLocation(shader.Program, "view"), 1, GL_FALSE, glm::value_ptr(view));
	// Draw the loaded model//fix for not dependint on cam
	model = glm::translate(model, glm::vec3(Pos.x, Pos.y - 3.0f, Pos.z)); // Translate it down a bit so it's at the center of the scene
	model = glm::scale(model, glm::vec3(3.0f, 3.0f, 3.0f));	// It's a bit too big for our scene, so scale it down
															//model = glm::rotate(model, -glm::radians(yaw) - (-glm::radians(90.0f)), glm::vec3(0, 1, 0));
	glUniformMatrix4fv(glGetUniformLocation(shader.Program, "model"), 1, GL_FALSE, glm::value_ptr(model));
	Model.Draw(shader);

}
void LoadFloor(Shader shader, glm::mat4 projection, Model Model, Camera camera) {
	glm::mat4 view1 = camera.GetViewMatrix();
	glm::mat4 model1;
	glm::vec3 Viewtest = camera.GetView();
	shader.Use();
	glUniformMatrix4fv(glGetUniformLocation(shader.Program, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
	glUniformMatrix4fv(glGetUniformLocation(shader.Program, "view"), 1, GL_FALSE, glm::value_ptr(view1));
	// Draw the loaded model
	model1 = glm::translate(model1, glm::vec3(1.0f, -10.0f, 1.0f)); // Translate it down a bit so it's at the center of the scene
	model1 = glm::scale(model1, glm::vec3(10.0f, 10.0f, 10.0f));	// It's a bit too big for our scene, so scale it down
	glUniformMatrix4fv(glGetUniformLocation(shader.Program, "model"), 1, GL_FALSE, glm::value_ptr(model1));
	Model.Draw(shader);

}
// Moves/alters the camera positions based on user input
void DoMovement()
{
	// Camera controls
	if (keys[GLFW_KEY_W]) {
		if (FirstCam) {
			camera.ProcessKeyboard(FORWARD, deltaTime);
		}
		PlayerPos.z -= 10.0f*deltaTime;
	}
	if (keys[GLFW_KEY_S]) {
		if (FirstCam) {
			camera.ProcessKeyboard(BACKWARD, deltaTime);
		}
		PlayerPos.z += 10.0f*deltaTime;
	}
	if (keys[GLFW_KEY_A]) {
		if (FirstCam) {
			camera.ProcessKeyboard(LEFT, deltaTime);
		}
		PlayerPos.x -= 10.0f*deltaTime;
	}
	if (keys[GLFW_KEY_D]) {
		if (FirstCam) {
			camera.ProcessKeyboard(RIGHT, deltaTime);
		}
		PlayerPos.x += 10.0f*deltaTime;
	}
}

// Is called whenever a key is pressed/released via GLFW
void KeyCallback(GLFWwindow *window, int key, int scancode, int action, int mode)
{
	if (GLFW_KEY_ESCAPE == key && GLFW_PRESS == action)
	{
		glfwSetWindowShouldClose(window, GL_TRUE);
	}

	if (key >= 0 && key < 1024)
	{
		if (action == GLFW_PRESS)
		{
			keys[key] = true;
		}
		else if (action == GLFW_RELEASE)
		{
			keys[key] = false;
		}
	}
}

void MouseCallback(GLFWwindow *window, double xPos, double yPos)
{
	if (firstMouse)
	{
		lastX = xPos;
		lastY = yPos;
		firstMouse = false;
	}

	GLfloat xOffset = xPos - lastX;
	GLfloat yOffset = lastY - yPos;  // Reversed since y-coordinates go from bottom to left

	lastX = xPos;
	lastY = yPos;

	camera.ProcessMouseMovement(xOffset, yOffset);
}

