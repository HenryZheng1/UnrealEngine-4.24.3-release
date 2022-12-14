// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using System.IO;

using Autodesk.Revit.DB.Lighting;
using Autodesk.Revit.DB.Visual;
using System;
using Autodesk.Revit.DB;
using System.Text;

namespace DatasmithRevitExporter
{
	// Light extraction and setup utilities.
	static class FDatasmithRevitLight
	{
		// Set the specific properties of the Datasmith light actor.
		static public void SetLightProperties(
			Asset                      in_lightAsset,
            Element                    in_sourceElement,
			FDatasmithFacadeActorLight io_datasmithLightActor
		)
		{
			// Set whether or not the Datasmith light is enabled.
			io_datasmithLightActor.SetEnabled(IsEnabled(in_lightAsset));

			double intensity = GetIntensity(in_lightAsset);
			if (intensity > 0)
			{
				// Set the Datasmith light intensity.
				io_datasmithLightActor.SetIntensity(intensity);
			}                

			// Set the Datasmith light linear color.
			var color = GetColor(in_lightAsset);
			io_datasmithLightActor.SetColor((float) color[0], (float) color[1], (float) color[2], 1.0F);
			
			double temperature = GetTemperature(in_lightAsset);
			if (temperature > 0)
			{
				// Set the Datasmith light temperature (in Kelvin degrees).
				io_datasmithLightActor.SetTemperature(temperature);
			}            

            var lightParameters = GetLightParameter(in_sourceElement);

            // Set the Datasmith light type.
            FDatasmithFacadeActorLight.ELightType lightType = GetLightType(lightParameters);
            io_datasmithLightActor.SetLightType(lightType);

			string iesFileName = GetIesFile(in_lightAsset);
            string iesCacheData = GetIesCacheData(in_lightAsset);

            if (iesFileName.Length > 0)
			{
				// Write a IES definition file and set its file path for the Datasmith light.
				// Use the user's temporary folder as IES definition file folder path.
				io_datasmithLightActor.WriteIESFile(Path.GetTempPath(), iesFileName, iesCacheData);
			}

			// Set the intensity unit of the Datasmith point light and derived types.
			// All lights in Revit seem to be in candelas by default.
			io_datasmithLightActor.SetPointIntensityUnit(FDatasmithFacadeActorLight.EPointLightIntensityUnit.Candelas);
			
			// Set the Datasmith area light shape.
			io_datasmithLightActor.SetAreaShape(ConvertLightShape(GetLightShape(in_lightAsset)));

			// Set the Datasmith area light distribution.
			io_datasmithLightActor.SetAreaType(ConvertLightType(GetLightDistribution(in_lightAsset)));

			float width = GetWidth(in_lightAsset);
			if (width > 0)
			{
				// Set the Datasmith area light shape size on the Y axis (in world units).
				io_datasmithLightActor.SetAreaWidth(width);
			}

			float length = GetLength(in_lightAsset);
			if (length > 0)
			{
				// Set the Datasmith area light shape size on the X axis (in world units).
				io_datasmithLightActor.SetAreaLength(length);
			}

            float innerConeAngle = GetInnerConeAngle(lightParameters);
            
            if (innerConeAngle > 0) { 
                io_datasmithLightActor.SetSpotInnerConeAngle(innerConeAngle);
            }
            float outerConeAngle = GetOuterConeAngle(lightParameters);
            if (outerConeAngle > 0) {
                io_datasmithLightActor.SetSpotOuterConeAngle(outerConeAngle);
            }
        } 

		/* Begin functions for IDatasmithLightActor */
		static bool IsEnabled(Asset asset)
		{
			AssetPropertyBoolean isEnabled = asset.FindByName("on") as AssetPropertyBoolean;
			if (isEnabled != null)
			{
				return isEnabled.Value;
			}
			return false;
		}

		static double GetIntensity(Asset asset)
		{
			AssetPropertyFloat intensity = asset.FindByName("intensityValue") as AssetPropertyFloat;
			if (intensity != null)
			{
				return (double)intensity.Value;
			}

			return -1.0;
		}

		static IList<double> GetColor(Asset asset)
		{
			AssetPropertyDoubleArray3d color = asset.FindByName("filterColor") as AssetPropertyDoubleArray3d;
			if (color != null)
			{
				return color.GetValueAsDoubles();
			}

			return null;
		}

		static double GetTemperature(Asset asset)
		{
			AssetPropertyFloat temperature = asset.FindByName("lightTemperature") as AssetPropertyFloat;
			if (temperature != null)
			{
				return (double)temperature.Value;
			}

			return -1.0;
		}

		//bool UseTemperature(Asset asset)

		static string GetIesFile(Asset asset)
		{
			AssetPropertyString iesFile = asset.FindByName("lightProfileFileName") as AssetPropertyString;

			if (iesFile != null)
			{
				return iesFile.Value;
			}

			return "";
		}

        static string GetIesCacheData(Asset asset)
        {
            AssetPropertyString iesFile = asset.FindByName("lightProfileCacheData") as AssetPropertyString;
            if (iesFile != null)
            {               
                byte[] data = Encoding.Unicode.GetBytes(iesFile.Value);             
                return Encoding.ASCII.GetString(data);                
            }
           
            return "";
        }

        /* End IDatasmithLightActorElement functions */
        /**********************************************/
        /* Begin IDatasmithPointLightElement functions*/

        //This function should return a enum but the corresponding enum haven't been found yet.
        static int GetInstensityUnits(Asset asset)
		{
			AssetPropertyEnum intensityUnits = asset.FindByName("intensityUnits") as AssetPropertyEnum;
			//Datamsith light units : Unitless, Candelas, Lumens
			if(intensityUnits != null)
			{
				return intensityUnits.Value;
			}


			return -1;
		}

        /*End IDatasmithPointLightElement function */
        /**********************************************/
        /* Begin IDatasmithSpotLightElement functions*/

        static float GetInnerConeAngle(IDictionary<string, string> parameters)
        {
            string key = "Spot Beam Angle";
            if (parameters.ContainsKey(key))
            {
                try
                {
                    var angleString = parameters[key].Replace("°", "");
                    var angle = float.Parse(angleString);
                    return angle/2.0f;
                }
                catch (Exception)
                {

                }
            }

            return -1.0f;
        }
        static float GetOuterConeAngle(IDictionary<string, string> parameters)
        {
            string key = "Spot Field Angle";
            if (parameters.ContainsKey(key))
            {
                try
                {
                    var angleString = parameters[key].Replace("°", "");
                    var angle = float.Parse(angleString);
                    return angle / 2.0f;
                }
                catch (Exception)
                {

                }
            }

            return -1.0f;
        }


        /* End IDatasmithSpotLightElement functions*/
        /**********************************************/
        /* Begin IDatasmithAreaLightElement functions*/

        static LightShapeStyle GetLightShape(Asset asset)
		{
			AssetPropertyEnum lightShapeType = asset.FindByName("lightobjectareatype") as AssetPropertyEnum;
			if(lightShapeType != null)
			{
				return (LightShapeStyle)lightShapeType.Value;
			}
			return LightShapeStyle.Point;
		}

		static LightDistributionStyle GetLightDistribution(Asset asset)
		{
			AssetPropertyEnum lightDistribution = asset.FindByName("distribution") as AssetPropertyEnum;
		   

			if (lightDistribution != null)
			{
				return (LightDistributionStyle)lightDistribution.Value;
			}
				
			return LightDistributionStyle.Spherical;
		}

		static float GetWidth(Asset asset)
		{
			AssetPropertyFloat width = asset.FindByName("rectangle_width") as AssetPropertyFloat;
			if (width != null)
				return width.Value;
			return -1.0f;
		}

		static float GetLength(Asset asset)
		{
			AssetPropertyFloat length = asset.FindByName("rectangle_length") as AssetPropertyFloat;
			if (length != null)
				return length.Value;

			return -1.0f;
		}

		/* End IDatasmithAreaLightElement functions*/
		/**********************************************/

		static private FDatasmithFacadeActorLight.EAreaLightShape ConvertLightShape(
			LightShapeStyle lss
		)
		{
			switch (lss)
			{
				case LightShapeStyle.Circle:
					return FDatasmithFacadeActorLight.EAreaLightShape.Disc;                  
				case LightShapeStyle.Line:
					return FDatasmithFacadeActorLight.EAreaLightShape.Cylinder;
				case LightShapeStyle.Point:
					return FDatasmithFacadeActorLight.EAreaLightShape.Sphere;
				case LightShapeStyle.Rectangle:
					return FDatasmithFacadeActorLight.EAreaLightShape.Rectangle;
				default:
					return FDatasmithFacadeActorLight.EAreaLightShape.Sphere;
			}
		}

		static private FDatasmithFacadeActorLight.EAreaLightType ConvertLightType(
			LightDistributionStyle lds
		)
		{
			switch (lds)
			{
				case LightDistributionStyle.Hemispherical:
					return FDatasmithFacadeActorLight.EAreaLightType.Point;
				case LightDistributionStyle.PhotometricWeb:
					return FDatasmithFacadeActorLight.EAreaLightType.IES_DEPRECATED;
				case LightDistributionStyle.Spherical:
					return FDatasmithFacadeActorLight.EAreaLightType.Point;
				case LightDistributionStyle.Spot:
					return FDatasmithFacadeActorLight.EAreaLightType.Spot;
				default:
					return FDatasmithFacadeActorLight.EAreaLightType.Point;
			}
		}

        static FDatasmithFacadeActorLight.ELightType GetLightType(IDictionary<string, string> parameters)
        {
            string key = "Light Source Definition (family)";

            if (parameters.ContainsKey(key))
            {
                if (parameters[key].Contains("Spot"))
                    return FDatasmithFacadeActorLight.ELightType.SpotLight;
                else if (parameters[key].Contains("Line") || parameters[key].Contains("Rectangle"))
                    return FDatasmithFacadeActorLight.ELightType.AreaLight;
            }          

            return FDatasmithFacadeActorLight.ELightType.PointLight;
        }

        static public IDictionary<string, string> GetLightParameter(
			Element in_sourceElement
		)
        {
            IDictionary<string, string> parametersMap = new Dictionary<string, string>();         

            IList<Parameter> parameters = in_sourceElement.GetOrderedParameters();
            if (parameters == null)
                return parametersMap;

            foreach (Parameter parameter in parameters)
            {

                if (parameter.HasValue)
                {
                    string value = parameter.AsValueString();
                    if (value == null)
                        value = parameter.AsString();
                    if (value == null || value.Length == 0)
                        continue;
                    string key = parameter.Definition.Name;
                    parametersMap.Add(key, value);
                }
            }

            return parametersMap;
        }
    }
}
