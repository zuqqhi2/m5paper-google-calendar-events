import * as functions from "firebase-functions";
import {calendar_v3, google} from "googleapis";
import * as path from "path";

const CALENDAR_SCOPE = "https://www.googleapis.com/auth/calendar.events.readonly";
const CALENDAR_ID = "PLEASE REPLACE HERE";
const LOGGER_OPTION = {structuredData: true};
const API_KEY = "PLEASE REPLACE HERE";
const AN1_FUNC = functions.region("asia-northeast1").https;
const DEFAULT_TIME = "1970-01-01T00:00:00+09:00";

type EventDateTimeSchema = calendar_v3.Schema$EventDateTime;
const convertEventTimeToDate = (eventTime?: EventDateTimeSchema) => {
  return new Date(eventTime?.dateTime ? eventTime.dateTime : DEFAULT_TIME);
};

export const getPlans = AN1_FUNC.onRequest((req, res) => {
  functions.logger.info("START Authentication", LOGGER_OPTION);
  functions.logger.info("x-api-key:", req.header("x-api-key"), LOGGER_OPTION);
  if (!req.header("x-api-key")) {
    const result = {
      status: "error",
      message: "Authentication failed",
    };
    functions.logger.info("Authentication failed", LOGGER_OPTION);
    res.status(400).send(result);
    return;
  } else if (req.header("x-api-key") != API_KEY) {
    const result = {
      status: "error",
      message: "Authentication failed",
    };
    functions.logger.info("x-api-key is not correct.", LOGGER_OPTION);
    res.status(400).send(result);
    return;
  }
  functions.logger.info("END Authentication", LOGGER_OPTION);

  functions.logger.info("START Generate client", LOGGER_OPTION);
  const client = generateClient();
  functions.logger.info("END Generate client", LOGGER_OPTION);

  functions.logger.info("START Get calendar events", LOGGER_OPTION);

  const timeMinDateTime = new Date();
  timeMinDateTime.setHours(0);
  timeMinDateTime.setMinutes(0);
  timeMinDateTime.setSeconds(0);
  const timeMin = timeMinDateTime.toISOString();

  const timeMaxDateTime = new Date();
  timeMaxDateTime.setDate(timeMaxDateTime.getDate() + 7); // Next 1 week events
  timeMaxDateTime.setHours(0);
  timeMaxDateTime.setMinutes(0);
  timeMaxDateTime.setSeconds(0);
  const timeMax = timeMaxDateTime.toISOString();

  functions.logger.info(`Time range: ${timeMin} - ${timeMax}`, LOGGER_OPTION);

  // https://github.com/googleapis/google-api-nodejs-client/blob/13d039d841a7788c35e953c8b345331adb0b3d4d/src/apis/calendar/v3.ts#L5689
  client.events.list({
    calendarId: CALENDAR_ID,
    timeMax: timeMax,
    timeMin: timeMin,
    maxResults: 5,
    // orderBy: "starttime",
    timeZone: "Asia/Tokyo",
  }).then((response) => {
    functions.logger.info("Result:", response, LOGGER_OPTION);
    functions.logger.info("Result items:", response.data.items, LOGGER_OPTION);
    functions.logger.info("END Get calendar events", LOGGER_OPTION);

    if (response.data.items === undefined) {
      const result = {
        status: "error",
        message: "Retrieved items are undefined",
      };
      functions.logger.info("Function result:", result, LOGGER_OPTION);
      return res.status(500).send(result);
    } else {
      // Sort events with start time ascending order
      const items = response.data.items.sort((a, b) => {
        const startTimeObjA = convertEventTimeToDate(a.start);
        const startTimeObjB = convertEventTimeToDate(b.start);
        if (startTimeObjA.getTime() > startTimeObjB.getTime()) {
          return 1;
        } else if (startTimeObjA.getTime() < startTimeObjB.getTime()) {
          return -1;
        } else {
          return 0;
        }
      });

      const result = {
        status: "success",
        num_items: response.data.items.length,
        items: items.map((item) => {
          const startTimeObj = convertEventTimeToDate(item.start);
          startTimeObj.setTime(startTimeObj.getTime() + 1000 * 60 * 60 * 9);
          const endTimeObj = convertEventTimeToDate(item.end);
          endTimeObj.setTime(endTimeObj.getTime() + 1000 * 60 * 60 * 9);

          let displayTime = startTimeObj.getFullYear() + "-";
          displayTime += ("0" + (startTimeObj.getMonth()+1)).slice(-2) + "-";
          displayTime += ("0" + startTimeObj.getDate()).slice(-2) + " ";
          displayTime += ("0" + startTimeObj.getHours()).slice(-2) + ":";
          displayTime += ("0" + startTimeObj.getMinutes()).slice(-2) + " - ";
          displayTime += ("0" + endTimeObj.getHours()).slice(-2) + ":";
          displayTime += ("0" + endTimeObj.getMinutes()).slice(-2);

          return {
            title: item.summary,
            startTime: item.start?.dateTime,
            endTime: item.end?.dateTime,
            displayTime: displayTime,
          };
        }),
      };
      functions.logger.info("Function result:", result, LOGGER_OPTION);
      return res.status(200).send(result);
    }
  }).catch((err) => {
    functions.logger.info(err, LOGGER_OPTION);

    const result = {
      status: "error",
      message: err.toString(),
    };
    functions.logger.info("Function result:", result, LOGGER_OPTION);
    return res.status(500).send(result);
  });
});

/**
 * Generate Google Calendar API Client
 * Service account credential is required.
 * Reference: https://github.com/googleapis/google-api-nodejs-client#service-account-credentials
 * @return {Calendar} Client
 */
const generateClient = () => {
  const auth = new google.auth.GoogleAuth({
    keyFile: path.join(__dirname, "credentials.json"),
    scopes: [CALENDAR_SCOPE],
  });

  return google.calendar({
    version: "v3",
    auth: auth,
  });
};
