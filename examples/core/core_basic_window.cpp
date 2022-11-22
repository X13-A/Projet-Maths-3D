﻿#include "raylib.h"
#include <raymath.h>
#include "rlgl.h"
#include <math.h>
#include <float.h>
#include <vector>

#include <iostream>
using namespace std;

#if defined(PLATFORM_DESKTOP)
#define GLSL_VERSION            330
#else   // PLATFORM_RPI, PLATFORM_ANDROID, PLATFORM_WEB
#define GLSL_VERSION            100
#endif

#define EPSILON 1.e-6f

template <typename T> int sgn(T val) {
	return (T(0) < val) - (val < T(0));
}

#pragma region Structs

struct ReferenceFrame {
	Vector3 origin;
	Vector3 i, j, k;
	Quaternion q;
	ReferenceFrame()
	{
		origin = { 0,0,0 };
		i = { 1,0,0 };
		j = { 0,1,0 };
		k = { 0,0,1 };
		q = QuaternionIdentity();
	}
	ReferenceFrame(Vector3 origin, Quaternion q)
	{
		this->q = q;
		this->origin = origin;
		i = Vector3RotateByQuaternion({ 1,0,0 }, q);
		j = Vector3RotateByQuaternion({ 0,1,0 }, q);
		k = Vector3RotateByQuaternion({ 0,0,1 }, q);
	}
	void Translate(Vector3 vect)
	{
		this->origin = Vector3Add(this->origin, vect);
	}
	void RotateByQuaternion(Quaternion qRot)
	{
		q = QuaternionMultiply(qRot, q);
		i = Vector3RotateByQuaternion({ 1,0,0 }, q);
		j = Vector3RotateByQuaternion({ 0,1,0 }, q);
		k = Vector3RotateByQuaternion({ 0,0,1 }, q);
	}
};

struct Quad {
	ReferenceFrame ref;
	Vector3 extents;
};

struct Plane
{
	Vector3 n;
	float d;
};

struct Disk {
	ReferenceFrame ref;
	float radius;
};

struct Sphere {
	ReferenceFrame ref;
	float radius;
};

struct Box {
	float width;
	float height;
	float depth;
	Vector3 centerPos;
};

struct Cylinder {
	ReferenceFrame ref;
	float halfHeight;
	float radius;
};

struct Capsule {
	ReferenceFrame ref;
	float halfHeight;
	float radius;
};

struct RoundedBox {
	ReferenceFrame ref;
	Vector3 extents;
	float radius;
};

struct Polar
{
	float rho;
	float theta;
};

struct Cylindrical
{
	float rho;
	float theta;
	float y;
};

struct Spherical
{
	float rho;
	float theta;
	float phi;
};

#pragma endregion

#pragma region Conversion

/*CartesianToPolar*/
Polar CartesianToPolar(Vector2 cart, bool keepThetaPositive = true)
{
	Polar polar = { Vector2Length(cart),atan2f(cart.y,cart.x) };
	if (keepThetaPositive && polar.theta < 0)
	{
		polar.theta += 2 * PI;
	}
	return polar;
}

/*PolarToCartesian*/
Vector2 PolarToCartesian(Polar polar)
{
	return Vector2Scale({ cosf(polar.theta),sinf(polar.theta) }, polar.rho);
}

/*CartesianToCylindrical*/
Cylindrical CartesianToCylindrical(Vector3 cart, bool keepThetaPositive = true)
{
	Cylindrical cylindrical = { sqrt(pow(cart.x, 2) + pow(cart.z, 2)), atan2f(cart.x,cart.z), cart.y };
	if (keepThetaPositive && cylindrical.theta < 0)
	{
		cylindrical.theta += 2 * PI;
	}

	return cylindrical;
}

/*CylindricalToCartesian*/
Vector3 CylindricalToCartesian(Cylindrical cylindrical)
{
	return Vector3({ cylindrical.rho * sinf(cylindrical.theta), cylindrical.y, cylindrical.rho * cosf(cylindrical.theta) });
}

/*CartesianToSpherical*/
Spherical CartesianToSpherical(Vector3 cart, bool keepThetaPositive = true)
{
	float rho = sqrt(pow(cart.x, 2) + pow(cart.y, 2) + pow(cart.z, 2));
	float phi = acosf(cart.y / rho);

	if (abs(rho) < EPSILON) phi = 0;
	else phi = acosf(cart.y / rho);

	Spherical spherical = { rho, atan2f(cart.x,cart.z), phi };
	if (keepThetaPositive && spherical.theta < 0)
	{
		spherical.theta += 2 * PI;
	}

	return spherical;
}

/*SphericalToCartesian*/
Vector3 SphericalToCartesian(Spherical spherical)
{
	return Vector3({ spherical.rho * sinf(spherical.phi) * sinf(spherical.theta), spherical.rho * cosf(spherical.phi), spherical.rho * sinf(spherical.phi) * cosf(spherical.theta) });
}

#pragma endregion

#pragma region Camera

void MyUpdateOrbitalCamera(Camera* camera, float deltaTime)
{
	static Spherical sphPos = { 10,PI / 4.f,PI / 4.f };
	Spherical sphSpeed = { 2 ,0.04f,0.04f };
	float rhoMin = 4;
	float rhoMax = 40;
	Vector2 mousePos;
	Spherical sphDelta;

	/*Calcul du vecteur de déplacement de la souris par différence entre la position courante de la souris et la position précédente*/
	static Vector2 prevMousePos = { 0, 0 };
	mousePos = GetMousePosition();
	Vector2 mouseVect = Vector2Subtract(mousePos, prevMousePos);
	prevMousePos = GetMousePosition();
	cout << "x: " << mouseVect.x << ", y: " << mouseVect.y << "\n";

	/*Calcul du vecteur de déplacement de la caméra en coordonnées sphériques*/
	Spherical cam = CartesianToSpherical(camera->position);
	cam.rho -= GetMouseWheelMove();
	/*Calcul de la nouvelle position de la caméra en coordonnées sphériques*/
	if (IsMouseButtonDown(MOUSE_RIGHT_BUTTON))
	{
		cam.theta -= mouseVect.x / 100;
		cam.phi -= mouseVect.y / 100;
	}
	camera->position = SphericalToCartesian(cam);
}

#pragma endregion

#pragma region Quad

void MyDrawPolygonQuad(Quad quad, Color color = LIGHTGRAY)
{
	int numVertex = 6;
	if (rlCheckBufferLimit(numVertex)) rlglDraw();

	// BEGINNING OF SPACE TRANSFORMATION INDUCED BY THE LOCAL REFERENCE FRAME
	// methods should be called in this order: rlTranslatef, rlRotatef & rlScalef
	// so that transformations occur in the opposite order: scale, then rotation, then translation
	rlPushMatrix();

	//TRANSLATION
	rlTranslatef(quad.ref.origin.x, quad.ref.origin.y, quad.ref.origin.z);

	//ROTATION
	Vector3 vect;
	float angle;
	QuaternionToAxisAngle(quad.ref.q, &vect, &angle);
	rlRotatef(angle * RAD2DEG, vect.x, vect.y, vect.z);

	//SCALING
	rlScalef(quad.extents.x, 1, quad.extents.z);
	// END OF SPACE TRANSFORMATION INDUCED BY THE LOCAL REFERENCE FRAME
	rlBegin(RL_TRIANGLES);
	rlColor4ub(color.r, color.g, color.b, color.a);
	rlVertex3f(1, 0, 1);
	rlVertex3f(1, 0, -1);
	rlVertex3f(-1, 0, -1);
	rlVertex3f(1, 0, 1);
	rlVertex3f(-1, 0, -1);
	rlVertex3f(-1, 0, 1);
	rlEnd();

	//EVERY rlPushMatrix method call should be followed by a rlPopMatrix method call
	rlPopMatrix();
}

void MyDrawWireframeQuad(Quad quad, Color color = DARKGRAY)
{
	int numVertex = 10;
	if (rlCheckBufferLimit(numVertex)) rlglDraw();

	rlPushMatrix();
	rlTranslatef(quad.ref.origin.x, quad.ref.origin.y, quad.ref.origin.z);

	Vector3 vect;
	float angle;
	QuaternionToAxisAngle(quad.ref.q, &vect, &angle);
	rlRotatef(angle * RAD2DEG, vect.x, vect.y, vect.z);

	rlScalef(quad.extents.x, 1, quad.extents.z);

	rlBegin(RL_LINES);
	rlColor4ub(color.r, color.g, color.b, color.a);

	rlVertex3f(1, 0, 1);
	rlVertex3f(1, 0, -1);

	rlVertex3f(1, 0, -1);
	rlVertex3f(-1, 0, -1);

	rlVertex3f(-1, 0, -1);
	rlVertex3f(1, 0, 1);

	rlVertex3f(-1, 0, -1);
	rlVertex3f(-1, 0, 1);

	rlVertex3f(-1, 0, 1);
	rlVertex3f(1, 0, 1);

	rlEnd();
	rlPopMatrix();
}

void MyDrawQuad(Quad quad, bool drawPolygon = true, bool drawWireframe = true, Color polygonColor = LIGHTGRAY, Color wireframeColor = DARKGRAY)
{
	if (drawPolygon) MyDrawPolygonQuad(quad, polygonColor);
	if (drawWireframe)MyDrawWireframeQuad(quad, wireframeColor);
}

#pragma endregion

#pragma region Disk

void MyDrawPolygonDisk(Disk disk, int nSectors, Color color = LIGHTGRAY)
{
	if (rlCheckBufferLimit(3 * nSectors)) rlglDraw();
	rlPushMatrix();
	rlTranslatef(disk.ref.origin.x, disk.ref.origin.y, disk.ref.origin.z);

	Vector3 vect;
	float angle;
	QuaternionToAxisAngle(disk.ref.q, &vect, &angle);
	rlRotatef(angle * RAD2DEG, vect.x, vect.y, vect.z);

	rlScalef(disk.radius, 1, disk.radius);

	rlBegin(RL_TRIANGLES);
	rlColor4ub(color.r, color.g, color.b, color.a);

	for (int i = 0; i < nSectors; i++)
	{
		Vector3 leftPoint = CylindricalToCartesian({ 1, (i * (2 * PI)) / nSectors, 0 });
		Vector3 rightPoint = CylindricalToCartesian({ 1, ((i + 1) * (2 * PI)) / nSectors, 0 });

		rlVertex3f(0, 0, 0);
		rlVertex3f(leftPoint.x, leftPoint.y, leftPoint.z);
		rlVertex3f(rightPoint.x, rightPoint.y, rightPoint.z);
	}

	rlEnd();
	rlPopMatrix();
}

void MyDrawWireframeDisk(Disk disk, int nSectors, Color color = DARKGRAY)
{
	int numVertex = 4;
	if (rlCheckBufferLimit(numVertex)) rlglDraw();

	rlPushMatrix();
	rlTranslatef(disk.ref.origin.x, disk.ref.origin.y, disk.ref.origin.z);

	Vector3 vect;
	float angle;
	QuaternionToAxisAngle(disk.ref.q, &vect, &angle);
	rlRotatef(angle * RAD2DEG, vect.x, vect.y, vect.z);

	rlScalef(disk.radius, 1, disk.radius);

	rlBegin(RL_LINES);
	rlColor4ub(color.r, color.g, color.b, color.a);

	for (int i = 0; i < nSectors; i++)
	{
		Vector3 leftPoint = CylindricalToCartesian({ 1, (i * (2 * PI)) / nSectors, 0 });
		Vector3 rightPoint = CylindricalToCartesian({ 1, ((i + 1) * (2 * PI)) / nSectors, 0 });

		rlVertex3f(0, 0, 0);
		rlVertex3f(leftPoint.x, leftPoint.y, leftPoint.z);

		rlVertex3f(leftPoint.x, leftPoint.y, leftPoint.z);
		rlVertex3f(rightPoint.x, rightPoint.y, rightPoint.z);
	}

	rlEnd();
	rlPopMatrix();
}

void MyDrawDisk(Disk disk, int nSectors, bool drawPolygon = true, bool drawWireframe = true, Color polygonColor = LIGHTGRAY, Color wireframeColor = DARKGRAY)
{
	if (drawPolygon) MyDrawPolygonDisk(disk,nSectors, polygonColor);
	if (drawWireframe)MyDrawWireframeDisk(disk, nSectors, wireframeColor);
}

#pragma endregion

#pragma region Box

void MyDrawPolygonBox(Box box, Color color = LIGHTGRAY) {
	//Valeur des points avant ajout des dimensions
	float ox = box.centerPos.x - box.width / 2;
	float oy = box.centerPos.y - box.height / 2;
	float oz = box.centerPos.z - box.depth / 2;

	//Valeur des points après ajout des dimensions
	float x = ox + box.width;
	float y = oy + box.height;
	float z = oz + box.depth;

	rlBegin(RL_LINES);
	rlColor4ub(color.r, color.g, color.b, color.a);

		//Front face
		rlVertex3f( ox, oy, oz );
		rlVertex3f( x, oy, oz );
		rlVertex3f( ox, y, oz );
		rlVertex3f( x, y, oz );

	rlEnd();
	rlPopMatrix();

	rlBegin(RL_LINES);
	rlColor4ub(color.r, color.g, color.b, color.a);

		//Top face
		rlVertex3f(ox, oy, oz);
		rlVertex3f(x, oy, oz);
		rlVertex3f(ox, y, oz);
		rlVertex3f(x, y, oz);

	rlEnd();
	rlPopMatrix();

		//Front face
		//DrawLine3D({ ox, oy, oz }, { x, oy, oz }, color);
		//DrawLine3D({ ox, oy, oz }, { ox, y, oz }, color);
		//DrawLine3D({ x, y, oz }, { ox, y, oz }, color);
		//DrawLine3D({ x, y, oz }, { x, oy, oz }, color);

		//Top face
		//DrawLine3D({ ox, y, oz }, { ox, y, z }, color);
		//DrawLine3D({ x, y, oz }, { x, y, z }, color);
		//DrawLine3D({ ox, y, z }, { x, y, z }, color);

		//Bottom face
		DrawLine3D({ ox, oy, oz }, { ox, oy, z }, color);
		DrawLine3D({ x, oy, oz }, { x, oy, z }, color);
		DrawLine3D({ ox, oy, z }, { x, oy, z }, color);

		//Back face
		DrawLine3D({ ox, oy, z }, { ox, y, z }, color);
		DrawLine3D({ x, oy, z }, { x, y, z }, color);

}

void MyDrawWireframeBox(Box box, Color color = DARKGRAY) {
	//Valeur des points avant ajout des dimensions
	float ox = box.centerPos.x - box.width / 2;
	float oy = box.centerPos.y - box.height / 2;
	float oz = box.centerPos.z - box.depth / 2;

	//Valeur des points après ajout des dimensions
	float x = ox + box.width;
	float y = oy + box.height;
	float z = oz + box.depth;

	//Front face
	DrawLine3D({ ox, oy, oz }, { x, oy, oz }, color);
	DrawLine3D({ ox, oy, oz }, { ox, y, oz }, color);
	DrawLine3D({ x, y, oz }, { ox, y, oz }, color);
	DrawLine3D({ x, y, oz }, { x, oy, oz }, color);

	//Top face
	DrawLine3D({ ox, y, oz }, { ox, y, z }, color);
	DrawLine3D({ x, y, oz }, { x, y, z }, color);
	DrawLine3D({ ox, y, z }, { x, y, z }, color);

	//Bottom face
	DrawLine3D({ ox, oy, oz }, { ox, oy, z }, color);
	DrawLine3D({ x, oy, oz }, { x, oy, z }, color);
	DrawLine3D({ ox, oy, z }, { x, oy, z }, color);

	//Back face
	DrawLine3D({ ox, oy, z }, { ox, y, z }, color);
	DrawLine3D({ x, oy, z }, { x, y, z }, color);
}

void MyDrawBox(Box box, bool drawPolygon = true, bool drawWireframe = true, Color polygonColor = LIGHTGRAY, Color wireframeColor = DARKGRAY) {
	if (drawWireframe) MyDrawWireframeBox(box, wireframeColor);
	if (drawPolygon) MyDrawPolygonBox(box, polygonColor);
}

#pragma endregion

#pragma region Sphere

void MyDrawPolygonSphere(Sphere sphere, int nMeridians, int nParallels, Color color = LIGHTGRAY)
{
	int numVertex = (nParallels + 2) * nMeridians * 6;
	if (rlCheckBufferLimit(numVertex)) rlglDraw();

	rlPushMatrix();

	rlTranslatef(sphere.radius, sphere.radius, sphere.radius);
	rlScalef(sphere.radius, sphere.radius, sphere.radius);

	rlBegin(RL_TRIANGLES);
	rlColor4ub(color.r, color.g, color.b, color.a);

	for (int i = 0; i < (nParallels + 2); i++)
	{
		for (int j = 0; j < nMeridians; j++)
		{
			rlVertex3f(cosf(DEG2RAD * (280 + (180 / (nParallels + 1)) * i)) * sinf(DEG2RAD * (j * 360 / nMeridians)),
				sinf(DEG2RAD * (280 + (180 / (nParallels + 1)) * i)),
				cosf(DEG2RAD * (280 + (180 / (nParallels + 1)) * i)) * cosf(DEG2RAD * (j * 360 / nMeridians)));
			rlVertex3f(cosf(DEG2RAD * (280 + (180 / (nParallels + 1)) * (i + 1))) * sinf(DEG2RAD * ((j + 1) * 360 / nMeridians)),
				sinf(DEG2RAD * (280 + (180 / (nParallels + 1)) * (i + 1))),
				cosf(DEG2RAD * (280 + (180 / (nParallels + 1)) * (i + 1))) * cosf(DEG2RAD * ((j + 1) * 360 / nMeridians)));
			rlVertex3f(cosf(DEG2RAD * (280 + (180 / (nParallels + 1)) * (i + 1))) * sinf(DEG2RAD * (j * 360 / nMeridians)),
				sinf(DEG2RAD * (280 + (180 / (nParallels + 1)) * (i + 1))),
				cosf(DEG2RAD * (280 + (180 / (nParallels + 1)) * (i + 1))) * cosf(DEG2RAD * (j * 360 / nMeridians)));

			rlVertex3f(cosf(DEG2RAD * (280 + (180 / (nParallels + 1)) * i)) * sinf(DEG2RAD * (j * 360 / nMeridians)),
				sinf(DEG2RAD * (280 + (180 / (nParallels + 1)) * i)),
				cosf(DEG2RAD * (280 + (180 / (nParallels + 1)) * i)) * cosf(DEG2RAD * (j * 360 / nMeridians)));
			rlVertex3f(cosf(DEG2RAD * (280 + (180 / (nParallels + 1)) * (i))) * sinf(DEG2RAD * ((j + 1) * 360 / nMeridians)),
				sinf(DEG2RAD * (280 + (180 / (nParallels + 1)) * (i))),
				cosf(DEG2RAD * (280 + (180 / (nParallels + 1)) * (i))) * cosf(DEG2RAD * ((j + 1) * 360 / nMeridians)));
			rlVertex3f(cosf(DEG2RAD * (280 + (180 / (nParallels + 1)) * (i + 1))) * sinf(DEG2RAD * ((j + 1) * 360 / nMeridians)),
				sinf(DEG2RAD * (280 + (180 / (nParallels + 1)) * (i + 1))),
				cosf(DEG2RAD * (280 + (180 / (nParallels + 1)) * (i + 1))) * cosf(DEG2RAD * ((j + 1) * 360 / nMeridians)));
		}
	}
	rlEnd();
	rlPopMatrix();
}

void MyDrawWireframeSphere(Sphere sphere, int nMeridians, int nParallels, Color color = DARKGRAY)
{
	int numVertex = (nParallels + 2) * nMeridians * 6;
	if (rlCheckBufferLimit(numVertex)) rlglDraw();

	rlPushMatrix();

	rlTranslatef(sphere.radius, sphere.radius, sphere.radius);
	rlScalef(sphere.radius, sphere.radius, sphere.radius);

	rlBegin(RL_LINES);
	rlColor4ub(color.r, color.g, color.b, color.a);

	for (int i = 0; i < (nParallels + 2); i++)
	{
		for (int j = 0; j < nMeridians; j++)
		{
			rlVertex3f(cosf(DEG2RAD * (270 + (180 / (nParallels + 1)) * (i + 1))) * sinf(DEG2RAD * ((j + 1) * 360 / nMeridians)),
				sinf(DEG2RAD * (270 + (180 / (nParallels + 1)) * (i + 1))),
				cosf(DEG2RAD * (270 + (180 / (nParallels + 1)) * (i + 1))) * cosf(DEG2RAD * ((j + 1) * 360 / nMeridians)));
			rlVertex3f(cosf(DEG2RAD * (270 + (180 / (nParallels + 1)) * (i + 1))) * sinf(DEG2RAD * (j * 360 / nMeridians)),
				sinf(DEG2RAD * (270 + (180 / (nParallels + 1)) * (i + 1))),
				cosf(DEG2RAD * (270 + (180 / (nParallels + 1)) * (i + 1))) * cosf(DEG2RAD * (j * 360 / nMeridians)));
			
			rlVertex3f(cosf(DEG2RAD * (280 + (180 / (nParallels + 1)) * i)) * sinf(DEG2RAD * (j * 360 / nMeridians)),
				sinf(DEG2RAD * (280 + (180 / (nParallels + 1)) * i)),
				cosf(DEG2RAD * (280 + (180 / (nParallels + 1)) * i)) * cosf(DEG2RAD * (j * 360 / nMeridians)));
			rlVertex3f(cosf(DEG2RAD * (280 + (180 / (nParallels + 1)) * (i + 1))) * sinf(DEG2RAD * (j * 360 / nMeridians)),
				sinf(DEG2RAD * (280 + (180 / (nParallels + 1)) * (i + 1))),
				cosf(DEG2RAD * (280 + (180 / (nParallels + 1)) * (i + 1))) * cosf(DEG2RAD * (j * 360 / nMeridians)));	
		}
	}
	rlEnd();
	rlPopMatrix();
}

void MyDrawSphere(Sphere sphere, int nMeridians, int nParallels, bool drawPolygon = true, bool drawWireframe = true, Color polygonColor = LIGHTGRAY, Color wireframeColor = DARKGRAY)
{
	if (drawPolygon) MyDrawPolygonSphere(sphere, nMeridians, nParallels, polygonColor);
	if (drawWireframe)MyDrawWireframeSphere(sphere, nMeridians, nParallels, wireframeColor);
}

#pragma endregion

#pragma region Cylinder

void MyDrawPolygonCylinder(Cylinder cylinder, int nSectors, bool drawCaps = false, Color color = LIGHTGRAY)
{
	if (rlCheckBufferLimit(4 * nSectors)) rlglDraw();
	rlPushMatrix();
	rlTranslatef(cylinder.ref.origin.x, cylinder.ref.origin.y, cylinder.ref.origin.z);

	Vector3 vect;
	float angle;
	QuaternionToAxisAngle(cylinder.ref.q, &vect, &angle);
	rlRotatef(angle * RAD2DEG, vect.x, vect.y, vect.z);

	rlScalef(cylinder.radius, 1, cylinder.radius);

	rlBegin(RL_TRIANGLES);
	rlColor4ub(color.r, color.g, color.b, color.a);

	for (int i = 0; i < nSectors; i++)
	{
		Vector3 leftPoint = CylindricalToCartesian({ 1, (i * (2 * PI)) / nSectors, 0 });
		Vector3 rightPoint = CylindricalToCartesian({ 1, ((i + 1) * (2 * PI)) / nSectors, 0 });

		rlVertex3f(0, 0, 0);
		rlVertex3f(leftPoint.x, leftPoint.y, leftPoint.z);
		rlVertex3f(rightPoint.x, rightPoint.y, rightPoint.z);
	}

	rlEnd();
	rlPopMatrix();
}
void MyDrawWireframeCylinder(Cylinder cylinder, int nSectors, bool drawCaps = false, Color color = LIGHTGRAY)
{

}
void MyDrawCylinder(Cylinder cylinder, int nSectors, bool drawCaps = false, bool drawPolygon = true, bool drawWireframe = true, Color polygonColor = LIGHTGRAY, Color wireframeColor = DARKGRAY)
{
	if (drawPolygon) MyDrawPolygonCylinder(cylinder, nSectors, drawCaps, polygonColor);
	if (drawWireframe)MyDrawWireframeCylinder(cylinder, nSectors, drawCaps, wireframeColor);
}

#pragma endregion

int main(int argc, char* argv[])
{
	// ICI LES TEST 

	// Initialization
	//--------------------------------------------------------------------------------------
	float screenSizeCoef = .9f;
	const int screenWidth = 1920 * screenSizeCoef;
	const int screenHeight = 1080 * screenSizeCoef;

	InitWindow(screenWidth, screenHeight, "ESIEE - E3FI - 2022 - 2023 - Maths 3D");

	SetTargetFPS(60);

	// CAMERA 
	Vector3 cameraPos = { 8, 15, 14 };
	Camera camera = { 0 };
	camera.position = cameraPos;
	camera.target = { 0, 0, 0 };
	camera.up = { 0, 1, 0 };
	camera.fovy = 120;
	camera.type = CAMERA_PERSPECTIVE;
	SetCameraMode(camera, CAMERA_CUSTOM);  // Set an orbital camera mode

	//--------------------------------------------------------------------------------------

	// Main game loop
	while (!WindowShouldClose())    // Detect window close button or ESC key
	{
		// Update
		//----------------------------------------------------------------------------------
		// TODO: Update your variables here
		//----------------------------------------------------------------------------------

		float deltaTime = GetFrameTime();
		float time = (float)GetTime();

		MyUpdateOrbitalCamera(&camera, deltaTime);

		// Draw
		//----------------------------------------------------------------------------------
		BeginDrawing();

		ClearBackground(RAYWHITE);

		BeginMode3D(camera);
		{
			//3D REFERENTIAL
			DrawGrid(20, 1);        // Draw a grid
			DrawLine3D({ 0 }, { 0,10,0 }, DARKGRAY);
			DrawSphere({ 10,0,0 }, .2f, RED);
			DrawSphere({ 0,10,0 }, .2f, GREEN);
			DrawSphere({ 0,0,10 }, .2f, BLUE);


			// DISK DISPLAY TEST
			/*ReferenceFrame ref = ReferenceFrame(
				{ 0,2,0 },
				QuaternionFromAxisAngle(Vector3Normalize({ 1,1,1 }), 0));
			Disk disk = { 4, ref };
			MyDrawDisk(disk, 30, false, true);*/

			// BOX DISPLAY TEST
			/*Box box = { 5,5,5, {0,0,0} };
			MyDrawBox(box, true, true, BLUE, BLACK);*/

			// QUAD DISPLAY TEST
			/*ReferenceFrame ref = ReferenceFrame(
				{ 2-,5,0 },
				QuaternionFromAxisAngle(Vector3Normalize({ 1,1,1 }), 90));
			Quad quad = { ref,{3,1,1} };
			MyDrawQuad(quad, true, false);*/

			// SPHERE DISPLAY TEST
			ReferenceFrame ref = ReferenceFrame(
				{ 0,2,0 },
				QuaternionFromAxisAngle(Vector3Normalize({ 1,1,1 }), 0));
			Sphere sphere = { ref, 5 };
			MyDrawSphere(sphere, 30, 20, true, true);

			// CYLINDER DISPLAY TEST
			/*ReferenceFrame ref = ReferenceFrame(
				{ 0,2,0 },
				QuaternionFromAxisAngle(Vector3Normalize({ 1,1,1 }), 0));
			Cylinder cylinder = { ref, 5, 3 };
			MyDrawPolygonCylinder(cylinder, 30);*/

		}
		EndMode3D();

		EndDrawing();
		//----------------------------------------------------------------------------------
	}

	// De-Initialization
	//--------------------------------------------------------------------------------------   
	CloseWindow();        // Close window and OpenGL context
	//--------------------------------------------------------------------------------------

	return 0;
}